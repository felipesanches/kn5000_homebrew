# Where the copied sources come from

This port starts from two code bases. Both were copied here unchanged in the first commit that adds them, so
`git log -p` on any of these files shows exactly what the KN7000 port changed.

| directory                                 | from                                                                                     | licence      |
|-------------------------------------------|------------------------------------------------------------------------------------------|--------------|
| `src/pt2/`                                | pt2-clone by Olav Sørensen (8bitbubsy), commit in `src/pt2/UPSTREAM`                     | BSD 3-Clause |
| `src/app/`, `src/common/`, `tools/build/` | the Pinball Fantasies port's platform (`~/compartilhado/Pinball/kn7000`, commit 05dd9e9) | GPLv3+       |

pt2-clone is a faithful C port of ProTracker 2.3D for the Amiga. Its `LICENSE` is in `src/pt2/LICENSE`.
