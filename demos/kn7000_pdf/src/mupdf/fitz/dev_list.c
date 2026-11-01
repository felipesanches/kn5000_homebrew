#include "fitz-internal.h"

typedef struct fz_display_node_s fz_display_node;

#define STACK_SIZE 96

typedef enum fz_display_command_e
{
	FZ_CMD_FILL_PATH,
	FZ_CMD_STROKE_PATH,
	FZ_CMD_CLIP_PATH,
	FZ_CMD_CLIP_STROKE_PATH,
	FZ_CMD_FILL_TEXT,
	FZ_CMD_STROKE_TEXT,
	FZ_CMD_CLIP_TEXT,
	FZ_CMD_CLIP_STROKE_TEXT,
	FZ_CMD_IGNORE_TEXT,
	FZ_CMD_FILL_SHADE,
	FZ_CMD_FILL_IMAGE,
	FZ_CMD_FILL_IMAGE_MASK,
	FZ_CMD_CLIP_IMAGE_MASK,
	FZ_CMD_POP_CLIP,
	FZ_CMD_BEGIN_MASK,
	FZ_CMD_END_MASK,
	FZ_CMD_BEGIN_GROUP,
	FZ_CMD_END_GROUP,
	FZ_CMD_BEGIN_TILE,
	FZ_CMD_END_TILE
} fz_display_command;

struct fz_display_node_s
{
	fz_display_command cmd;
	fz_display_node *next;
	fz_rect rect;
	union {
		fz_path *path;
		fz_text *text;
		fz_shade *shade;
		fz_image *image;
		int blendmode;
	} item;
	fz_stroke_state *stroke;
	int flag; /* even_odd, accumulate, isolated/knockout... */
	fz_matrix ctm;
	fz_colorspace *colorspace;
	float alpha;
	float color[FZ_MAX_COLORS];
#ifdef KN7PDF
	fz_bbox kn7_bbox; /* rect covered in whole pixels, once (kn7_have): what each band's visibility test reads */
	int kn7_have;
#endif
};

struct fz_display_list_s
{
	fz_display_node *first;
	fz_display_node *last;

	int top;
	struct {
		fz_rect *update;
		fz_rect rect;
	} stack[STACK_SIZE];
	int tiled;
#ifdef KN7PDF
	int kn7_culling;
	fz_rect kn7_keep;
#endif
};

enum { ISOLATED = 1, KNOCKOUT = 2 };

#ifdef KN7PDF
/* KN7PDF: a list of a region of the page only (fz_kn7_list_keep): what is drawn wholly outside `keep` is left out as
 * the page is run into the list, before a node is made for it, so that the list of a page too large for the KN7000's
 * memory can be made a region at a time (src/kn7/view.c). Clips, groups, masks and tiles are always kept: their pairs
 * must match. */
void
fz_kn7_list_keep(fz_display_list *list, fz_rect keep)
{
	list->kn7_culling = 1;
	list->kn7_keep = keep;
}

static int
kn7_culled(fz_device *dev, fz_rect r)
{
	fz_display_list *list = dev->user;
	return list->kn7_culling && fz_is_empty_rect(fz_intersect_rect(r, list->kn7_keep));
}
#endif

static fz_display_node *
fz_new_display_node(fz_context *ctx, fz_display_command cmd, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	int i;

	node = fz_malloc_struct(ctx, fz_display_node);
	node->cmd = cmd;
	node->next = NULL;
	node->rect = fz_empty_rect;
	node->item.path = NULL;
	node->stroke = NULL;
	node->flag = 0;
	node->ctm = ctm;
	if (colorspace)
	{
		node->colorspace = fz_keep_colorspace(ctx, colorspace);
		if (color)
		{
			for (i = 0; i < node->colorspace->n; i++)
				node->color[i] = color[i];
		}
	}
	else
	{
		node->colorspace = NULL;
	}
	node->alpha = alpha;

	return node;
}

static void
fz_append_display_node(fz_display_list *list, fz_display_node *node)
{
	switch (node->cmd)
	{
	case FZ_CMD_CLIP_PATH:
	case FZ_CMD_CLIP_STROKE_PATH:
	case FZ_CMD_CLIP_IMAGE_MASK:
		if (list->top < STACK_SIZE)
		{
			list->stack[list->top].update = &node->rect;
			list->stack[list->top].rect = fz_empty_rect;
		}
		list->top++;
		break;
	case FZ_CMD_END_MASK:
	case FZ_CMD_CLIP_TEXT:
	case FZ_CMD_CLIP_STROKE_TEXT:
		if (list->top < STACK_SIZE)
		{
			list->stack[list->top].update = NULL;
			list->stack[list->top].rect = fz_empty_rect;
		}
		list->top++;
		break;
	case FZ_CMD_BEGIN_TILE:
		list->tiled++;
		if (list->top > 0 && list->top <= STACK_SIZE)
		{
			list->stack[list->top-1].rect = fz_infinite_rect;
		}
		break;
	case FZ_CMD_END_TILE:
		list->tiled--;
		break;
	case FZ_CMD_END_GROUP:
		break;
	case FZ_CMD_POP_CLIP:
		if (list->top > STACK_SIZE)
		{
			list->top--;
			node->rect = fz_infinite_rect;
		}
		else if (list->top > 0)
		{
			fz_rect *update;
			list->top--;
			update = list->stack[list->top].update;
			if (list->tiled == 0)
			{
				if (update)
				{
					*update = fz_intersect_rect(*update, list->stack[list->top].rect);
					node->rect = *update;
				}
				else
					node->rect = list->stack[list->top].rect;
			}
			else
				node->rect = fz_infinite_rect;
		}
		/* fallthrough */
	default:
		if (list->top > 0 && list->tiled == 0 && list->top <= STACK_SIZE)
			list->stack[list->top-1].rect = fz_union_rect(list->stack[list->top-1].rect, node->rect);
		break;
	}
	if (!list->first)
	{
		list->first = node;
		list->last = node;
	}
	else
	{
		list->last->next = node;
		list->last = node;
	}
}

static void
fz_free_display_node(fz_context *ctx, fz_display_node *node)
{
	switch (node->cmd)
	{
	case FZ_CMD_FILL_PATH:
	case FZ_CMD_STROKE_PATH:
	case FZ_CMD_CLIP_PATH:
	case FZ_CMD_CLIP_STROKE_PATH:
		fz_free_path(ctx, node->item.path);
		break;
	case FZ_CMD_FILL_TEXT:
	case FZ_CMD_STROKE_TEXT:
	case FZ_CMD_CLIP_TEXT:
	case FZ_CMD_CLIP_STROKE_TEXT:
	case FZ_CMD_IGNORE_TEXT:
		fz_free_text(ctx, node->item.text);
		break;
	case FZ_CMD_FILL_SHADE:
		fz_drop_shade(ctx, node->item.shade);
		break;
	case FZ_CMD_FILL_IMAGE:
	case FZ_CMD_FILL_IMAGE_MASK:
	case FZ_CMD_CLIP_IMAGE_MASK:
		fz_drop_image(ctx, node->item.image);
		break;
	case FZ_CMD_POP_CLIP:
	case FZ_CMD_BEGIN_MASK:
	case FZ_CMD_END_MASK:
	case FZ_CMD_BEGIN_GROUP:
	case FZ_CMD_END_GROUP:
	case FZ_CMD_BEGIN_TILE:
	case FZ_CMD_END_TILE:
		break;
	}
	if (node->stroke)
		fz_drop_stroke_state(ctx, node->stroke);
	if (node->colorspace)
		fz_drop_colorspace(ctx, node->colorspace);
	fz_free(ctx, node);
}

#ifdef KN7PDF
/* KN7PDF: a fill merged into the fill just before it in the list when nothing tells them apart (the same fill rule,
 * matrix, colour space, colour and alpha) and it touches none of the pixels the other covers (their bounding boxes in
 * whole pixels): each pixel's coverage is then one shape's, so the page comes out the same, and the list holds one node and one path for a run of small
 * shapes (a contents page's dotted leaders are thousands of dots, each its own fill: 2150 nodes, 400 KB). The run
 * stays within KN7_MERGE_ROWS rows, so that each band flattens little more than it draws. */
#define KN7_MERGE_ROWS 24
static int
kn7_merge_fill(fz_context *ctx, fz_display_list *list, fz_path *path, int even_odd, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha, fz_rect r)
{
	fz_display_node *n = list->last;
	fz_path *p;
	fz_rect u;
	int i, need;

	if (!n || n->cmd != FZ_CMD_FILL_PATH || n->flag != even_odd || n->colorspace != colorspace || n->alpha != alpha ||
		memcmp(&n->ctm, &ctm, sizeof ctm) || !path->len || path->items[0].k != FZ_MOVETO || fz_is_empty_rect(r))
		return 0;
	for (i = 0; colorspace && color && i < colorspace->n; i++)
		if (n->color[i] != color[i])
			return 0;
	{
		/* (the pixels each covers: none in common) */
		fz_bbox a = fz_bbox_covering_rect(n->rect), b = fz_bbox_covering_rect(r);
		if (b.x0 < a.x1 && b.x1 > a.x0 && b.y0 < a.y1 && b.y1 > a.y0)
			return 0;
	}
	u = fz_union_rect(n->rect, r);
	if (u.y1 - u.y0 > KN7_MERGE_ROWS)
		return 0;
	p = n->item.path;
	need = p->len + path->len;
	if (need > p->cap)
	{
		int cap = need + need / 4;
		fz_path_item *items = fz_resize_array_no_throw(ctx, p->items, cap, sizeof(fz_path_item));
		if (!items)
			return 0;  /* (a node of its own, then) */
		p->items = items;
		p->cap = cap;
	}
	memcpy(p->items + p->len, path->items, path->len * sizeof(fz_path_item));
	p->len = need;
	n->rect = u;
	n->kn7_have = 0;
	if (list->top > 0 && list->tiled == 0 && list->top <= STACK_SIZE)
		list->stack[list->top-1].rect = fz_union_rect(list->stack[list->top-1].rect, r);
	return 1;
}
#endif

static void
fz_list_fill_path(fz_device *dev, fz_path *path, int even_odd, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
#ifdef KN7PDF
	fz_rect r = fz_bound_path(dev->ctx, path, NULL, ctm);
	if (kn7_culled(dev, r))
		return;
	if (kn7_merge_fill(ctx, dev->user, path, even_odd, ctm, colorspace, color, alpha, r))
		return;
#endif
	node = fz_new_display_node(ctx, FZ_CMD_FILL_PATH, ctm, colorspace, color, alpha);
	fz_try(ctx)
	{
#ifdef KN7PDF
		node->rect = r;
#else
		node->rect = fz_bound_path(dev->ctx, path, NULL, ctm);
#endif
		node->item.path = fz_clone_path(dev->ctx, path);
		node->flag = even_odd;
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_stroke_path(fz_device *dev, fz_path *path, fz_stroke_state *stroke, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
#ifdef KN7PDF
	fz_rect r = fz_bound_path(dev->ctx, path, stroke, ctm);
	if (kn7_culled(dev, r))
		return;
#endif
	node = fz_new_display_node(ctx, FZ_CMD_STROKE_PATH, ctm, colorspace, color, alpha);
	fz_try(ctx)
	{
#ifdef KN7PDF
		node->rect = r;
#else
		node->rect = fz_bound_path(dev->ctx, path, stroke, ctm);
#endif
		node->item.path = fz_clone_path(dev->ctx, path);
		node->stroke = fz_keep_stroke_state(dev->ctx, stroke);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_path(fz_device *dev, fz_path *path, fz_rect *rect, int even_odd, fz_matrix ctm)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_CLIP_PATH, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
		node->rect = fz_bound_path(dev->ctx, path, NULL, ctm);
		if (rect)
			node->rect = fz_intersect_rect(node->rect, *rect);
		node->item.path = fz_clone_path(dev->ctx, path);
		node->flag = even_odd;
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_stroke_path(fz_device *dev, fz_path *path, fz_rect *rect, fz_stroke_state *stroke, fz_matrix ctm)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_CLIP_STROKE_PATH, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
		node->rect = fz_bound_path(dev->ctx, path, stroke, ctm);
		if (rect)
			node->rect = fz_intersect_rect(node->rect, *rect);
		node->item.path = fz_clone_path(dev->ctx, path);
		node->stroke = fz_keep_stroke_state(dev->ctx, stroke);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_fill_text(fz_device *dev, fz_text *text, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
#ifdef KN7PDF
	fz_rect r = fz_bound_text(dev->ctx, text, ctm);
	if (kn7_culled(dev, r))
		return;
#endif
	node = fz_new_display_node(ctx, FZ_CMD_FILL_TEXT, ctm, colorspace, color, alpha);
	fz_try(ctx)
	{
#ifdef KN7PDF
		node->rect = r;
#else
		node->rect = fz_bound_text(dev->ctx, text, ctm);
#endif
		node->item.text = fz_clone_text(dev->ctx, text);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_stroke_text(fz_device *dev, fz_text *text, fz_stroke_state *stroke, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
#ifdef KN7PDF
	fz_rect r = fz_bound_text(dev->ctx, text, ctm);
	if (kn7_culled(dev, r))
		return;
#endif
	node = fz_new_display_node(ctx, FZ_CMD_STROKE_TEXT, ctm, colorspace, color, alpha);
	node->item.text = NULL;
	fz_try(ctx)
	{
#ifdef KN7PDF
		node->rect = r;
#else
		node->rect = fz_bound_text(dev->ctx, text, ctm);
#endif
		fz_adjust_rect_for_stroke(&node->rect, stroke, &ctm);
		node->item.text = fz_clone_text(dev->ctx, text);
		node->stroke = fz_keep_stroke_state(dev->ctx, stroke);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_text(fz_device *dev, fz_text *text, fz_matrix ctm, int accumulate)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_CLIP_TEXT, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
		node->rect = fz_bound_text(dev->ctx, text, ctm);
		node->item.text = fz_clone_text(dev->ctx, text);
		node->flag = accumulate;
		/* when accumulating, be conservative about culling */
		if (accumulate)
			node->rect = fz_infinite_rect;
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_stroke_text(fz_device *dev, fz_text *text, fz_stroke_state *stroke, fz_matrix ctm)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_CLIP_STROKE_TEXT, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
		node->rect = fz_bound_text(dev->ctx, text, ctm);
		fz_adjust_rect_for_stroke(&node->rect, stroke, &ctm);
		node->item.text = fz_clone_text(dev->ctx, text);
		node->stroke = fz_keep_stroke_state(dev->ctx, stroke);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_ignore_text(fz_device *dev, fz_text *text, fz_matrix ctm)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
#ifdef KN7PDF
	fz_rect r = fz_bound_text(dev->ctx, text, ctm);
	if (kn7_culled(dev, r))
		return;
#endif
	node = fz_new_display_node(ctx, FZ_CMD_IGNORE_TEXT, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
#ifdef KN7PDF
		node->rect = r;
#else
		node->rect = fz_bound_text(dev->ctx, text, ctm);
#endif
		node->item.text = fz_clone_text(dev->ctx, text);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_pop_clip(fz_device *dev)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_POP_CLIP, fz_identity, NULL, NULL, 0);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_fill_shade(fz_device *dev, fz_shade *shade, fz_matrix ctm, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
#ifdef KN7PDF
	fz_rect r = fz_bound_shade(ctx, shade, ctm);
	if (kn7_culled(dev, r))
		return;
#endif
	node = fz_new_display_node(ctx, FZ_CMD_FILL_SHADE, ctm, NULL, NULL, alpha);
	node->rect = fz_bound_shade(ctx, shade, ctm);
	node->item.shade = fz_keep_shade(ctx, shade);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_fill_image(fz_device *dev, fz_image *image, fz_matrix ctm, float alpha)
{
	fz_display_node *node;
#ifdef KN7PDF
	if (kn7_culled(dev, fz_transform_rect(ctm, fz_unit_rect)))
		return;
#endif
	node = fz_new_display_node(dev->ctx, FZ_CMD_FILL_IMAGE, ctm, NULL, NULL, alpha);
	node->rect = fz_transform_rect(ctm, fz_unit_rect);
	node->item.image = fz_keep_image(dev->ctx, image);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_fill_image_mask(fz_device *dev, fz_image *image, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
#ifdef KN7PDF
	if (kn7_culled(dev, fz_transform_rect(ctm, fz_unit_rect)))
		return;
#endif
	node = fz_new_display_node(dev->ctx, FZ_CMD_FILL_IMAGE_MASK, ctm, colorspace, color, alpha);
	node->rect = fz_transform_rect(ctm, fz_unit_rect);
	node->item.image = fz_keep_image(dev->ctx, image);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_image_mask(fz_device *dev, fz_image *image, fz_rect *rect, fz_matrix ctm)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_CLIP_IMAGE_MASK, ctm, NULL, NULL, 0);
	node->rect = fz_transform_rect(ctm, fz_unit_rect);
	if (rect)
		node->rect = fz_intersect_rect(node->rect, *rect);
	node->item.image = fz_keep_image(dev->ctx, image);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_begin_mask(fz_device *dev, fz_rect rect, int luminosity, fz_colorspace *colorspace, float *color)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_BEGIN_MASK, fz_identity, colorspace, color, 0);
	node->rect = rect;
	node->flag = luminosity;
	fz_append_display_node(dev->user, node);
}

static void
fz_list_end_mask(fz_device *dev)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_END_MASK, fz_identity, NULL, NULL, 0);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_begin_group(fz_device *dev, fz_rect rect, int isolated, int knockout, int blendmode, float alpha)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_BEGIN_GROUP, fz_identity, NULL, NULL, alpha);
	node->rect = rect;
	node->item.blendmode = blendmode;
	node->flag |= isolated ? ISOLATED : 0;
	node->flag |= knockout ? KNOCKOUT : 0;
	fz_append_display_node(dev->user, node);
}

static void
fz_list_end_group(fz_device *dev)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_END_GROUP, fz_identity, NULL, NULL, 0);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_begin_tile(fz_device *dev, fz_rect area, fz_rect view, float xstep, float ystep, fz_matrix ctm)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_BEGIN_TILE, ctm, NULL, NULL, 0);
	node->rect = area;
	node->color[0] = xstep;
	node->color[1] = ystep;
	node->color[2] = view.x0;
	node->color[3] = view.y0;
	node->color[4] = view.x1;
	node->color[5] = view.y1;
	fz_append_display_node(dev->user, node);
}

static void
fz_list_end_tile(fz_device *dev)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_END_TILE, fz_identity, NULL, NULL, 0);
	fz_append_display_node(dev->user, node);
}

fz_device *
fz_new_list_device(fz_context *ctx, fz_display_list *list)
{
	fz_device *dev = fz_new_device(ctx, list);

	dev->fill_path = fz_list_fill_path;
	dev->stroke_path = fz_list_stroke_path;
	dev->clip_path = fz_list_clip_path;
	dev->clip_stroke_path = fz_list_clip_stroke_path;

	dev->fill_text = fz_list_fill_text;
	dev->stroke_text = fz_list_stroke_text;
	dev->clip_text = fz_list_clip_text;
	dev->clip_stroke_text = fz_list_clip_stroke_text;
	dev->ignore_text = fz_list_ignore_text;

	dev->fill_shade = fz_list_fill_shade;
	dev->fill_image = fz_list_fill_image;
	dev->fill_image_mask = fz_list_fill_image_mask;
	dev->clip_image_mask = fz_list_clip_image_mask;

	dev->pop_clip = fz_list_pop_clip;

	dev->begin_mask = fz_list_begin_mask;
	dev->end_mask = fz_list_end_mask;
	dev->begin_group = fz_list_begin_group;
	dev->end_group = fz_list_end_group;

	dev->begin_tile = fz_list_begin_tile;
	dev->end_tile = fz_list_end_tile;

	return dev;
}

fz_display_list *
fz_new_display_list(fz_context *ctx)
{
	fz_display_list *list = fz_malloc_struct(ctx, fz_display_list);
	list->first = NULL;
	list->last = NULL;
	list->top = 0;
	list->tiled = 0;
	return list;
}

void
fz_free_display_list(fz_context *ctx, fz_display_list *list)
{
	fz_display_node *node;

	if (list == NULL)
		return;
	node = list->first;
	while (node)
	{
		fz_display_node *next = node->next;
		fz_free_display_node(ctx, node);
		node = next;
	}
	fz_free(ctx, list);
}

void
fz_run_display_list(fz_display_list *list, fz_device *dev, fz_matrix top_ctm, fz_bbox scissor, fz_cookie *cookie)
{
	fz_display_node *node;
	fz_matrix ctm;
	fz_rect rect;
	fz_bbox bbox;
	int clipped = 0;
	int tiled = 0;
	int empty;
	int progress = 0;
	fz_context *ctx = dev->ctx;

#ifdef KN7PDF
	const int kn7_identity = top_ctm.a == 1 && top_ctm.b == 0 && top_ctm.c == 0 && top_ctm.d == 1 && top_ctm.e == 0 &&
		top_ctm.f == 0;
#endif

	if (cookie)
	{
		cookie->progress_max = list->last - list->first;
		cookie->progress = 0;
	}

	for (node = list->first; node; node = node->next)
	{
#ifdef KN7PDF
		KN7_YIELD();
#endif
		/* Check the cookie for aborting */
		if (cookie)
		{
			if (cookie->abort)
				break;
			cookie->progress = progress++;
		}

		/* cull objects to draw using a quick visibility test */

		if (tiled || node->cmd == FZ_CMD_BEGIN_TILE || node->cmd == FZ_CMD_END_TILE)
		{
			empty = 0;
		}
#ifdef KN7PDF
		/* KN7PDF: with no transform (the viewer's bands), the node's pixels once, then integer tests: the floating
		 * point here, every node for every band, was much of a page's time on the KN7000, which has no FPU */
		else if (kn7_identity)
		{
			if (!node->kn7_have)
			{
				node->kn7_bbox = fz_bbox_covering_rect(node->rect);
				node->kn7_have = 1;
			}
			bbox = fz_intersect_bbox(node->kn7_bbox, scissor);
			empty = fz_is_empty_bbox(bbox);
		}
#endif
		else
		{
			bbox = fz_bbox_covering_rect(fz_transform_rect(top_ctm, node->rect));
			bbox = fz_intersect_bbox(bbox, scissor);
			empty = fz_is_empty_bbox(bbox);
		}

		if (clipped || empty)
		{
			switch (node->cmd)
			{
			case FZ_CMD_CLIP_PATH:
			case FZ_CMD_CLIP_STROKE_PATH:
			case FZ_CMD_CLIP_STROKE_TEXT:
			case FZ_CMD_CLIP_IMAGE_MASK:
			case FZ_CMD_BEGIN_MASK:
			case FZ_CMD_BEGIN_GROUP:
				clipped++;
				continue;
			case FZ_CMD_CLIP_TEXT:
				/* Accumulated text has no extra pops */
				if (node->flag != 2)
					clipped++;
				continue;
			case FZ_CMD_POP_CLIP:
			case FZ_CMD_END_GROUP:
				if (!clipped)
					goto visible;
				clipped--;
				continue;
			case FZ_CMD_END_MASK:
				if (!clipped)
					goto visible;
				continue;
			default:
				continue;
			}
		}

visible:
#ifdef KN7PDF
		ctm = kn7_identity ? node->ctm : fz_concat(node->ctm, top_ctm);
#else
		ctm = fz_concat(node->ctm, top_ctm);
#endif

		fz_try(ctx)
		{
			switch (node->cmd)
			{
			case FZ_CMD_FILL_PATH:
				fz_fill_path(dev, node->item.path, node->flag, ctm,
					node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_STROKE_PATH:
				fz_stroke_path(dev, node->item.path, node->stroke, ctm,
					node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_CLIP_PATH:
			{
				fz_rect trect = fz_transform_rect(top_ctm, node->rect);
				fz_clip_path(dev, node->item.path, &trect, node->flag, ctm);
				break;
			}
			case FZ_CMD_CLIP_STROKE_PATH:
			{
				fz_rect trect = fz_transform_rect(top_ctm, node->rect);
				fz_clip_stroke_path(dev, node->item.path, &trect, node->stroke, ctm);
				break;
			}
			case FZ_CMD_FILL_TEXT:
				fz_fill_text(dev, node->item.text, ctm,
					node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_STROKE_TEXT:
				fz_stroke_text(dev, node->item.text, node->stroke, ctm,
					node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_CLIP_TEXT:
				fz_clip_text(dev, node->item.text, ctm, node->flag);
				break;
			case FZ_CMD_CLIP_STROKE_TEXT:
				fz_clip_stroke_text(dev, node->item.text, node->stroke, ctm);
				break;
			case FZ_CMD_IGNORE_TEXT:
				fz_ignore_text(dev, node->item.text, ctm);
				break;
			case FZ_CMD_FILL_SHADE:
				fz_fill_shade(dev, node->item.shade, ctm, node->alpha);
				break;
			case FZ_CMD_FILL_IMAGE:
				fz_fill_image(dev, node->item.image, ctm, node->alpha);
				break;
			case FZ_CMD_FILL_IMAGE_MASK:
				fz_fill_image_mask(dev, node->item.image, ctm,
					node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_CLIP_IMAGE_MASK:
			{
				fz_rect trect = fz_transform_rect(top_ctm, node->rect);
				fz_clip_image_mask(dev, node->item.image, &trect, ctm);
				break;
			}
			case FZ_CMD_POP_CLIP:
				fz_pop_clip(dev);
				break;
			case FZ_CMD_BEGIN_MASK:
				rect = fz_transform_rect(top_ctm, node->rect);
				fz_begin_mask(dev, rect, node->flag, node->colorspace, node->color);
				break;
			case FZ_CMD_END_MASK:
				fz_end_mask(dev);
				break;
			case FZ_CMD_BEGIN_GROUP:
				rect = fz_transform_rect(top_ctm, node->rect);
				fz_begin_group(dev, rect,
					(node->flag & ISOLATED) != 0, (node->flag & KNOCKOUT) != 0,
					node->item.blendmode, node->alpha);
				break;
			case FZ_CMD_END_GROUP:
				fz_end_group(dev);
				break;
			case FZ_CMD_BEGIN_TILE:
				tiled++;
				rect.x0 = node->color[2];
				rect.y0 = node->color[3];
				rect.x1 = node->color[4];
				rect.y1 = node->color[5];
				fz_begin_tile(dev, node->rect, rect,
					node->color[0], node->color[1], ctm);
				break;
			case FZ_CMD_END_TILE:
				tiled--;
				fz_end_tile(dev);
				break;
			}
		}
		fz_catch(ctx)
		{
			/* Swallow the error */
			if (cookie)
				cookie->errors++;
			fz_warn(ctx, "Ignoring error during interpretation");
		}
	}
}
