/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * kn7ftmodules.h -- FreeType's modules for the viewer: MuPDF's slimftmodules.h less the CID-keyed Type 1 driver (fonts
 * of that kind are rare in PDFs, which carry CID fonts as CFF), the PostScript hinter and the monochrome rasterizer
 * (MuPDF 1.1 asks for neither while it anti-aliases: res_font.c). */
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t1_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
