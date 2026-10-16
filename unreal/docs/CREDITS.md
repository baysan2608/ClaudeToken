# Credits

Third-party content in the Unreal build of Fourfold. Generated assets are rebuilt by the scripts named here; each download
also records its source and licence in a git-ignored manifest next to it.

## Jungle Ruins map (L_Ruins)

- **"Jungle Ruins"** scene, version 1.0.1b, by **Cristiano Siqueira**, with artistic support by **Charlene Teets** —
  Intel Sample Library (https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-processing-research/samples.html).
- Licence: Creative Commons Attribution 4.0 International (CC BY 4.0), https://creativecommons.org/licenses/by/4.0/.
- Changes made for Fourfold: a 288 m core cut from the cinematic terrain tile and re-meshed with LODs; the pyramid given
  LODs and repacked textures; the jungle thinned and re-scattered from the scene's own plants with new LODs; terrain and
  plant textures cropped and resized; river / creek surfaces use Fourfold's water material; lighting is Fourfold's.
- Fetched by `unreal/Tools/ruins/fetch_ruins.py`: source URL, size, sha256 and the licence file shipped with the scene are
  kept in `unreal/SourceArt/OpenWorld/JungleRuins/ruins.json` and `credits_license.txt` (git-ignored).
- In game: Settings > Credits.

## Poly Haven (CC0)

Textures, trees, rocks and ground-cover scans of L_Lab and L_World come from Poly Haven (https://polyhaven.com, CC0 1.0, no
attribution required; authors listed per asset in the git-ignored manifests `unreal/SourceArt/Environment/PolyHaven/polyhaven.json`,
`unreal/SourceArt/OpenWorld/PolyHaven/polyhaven.json`, `unreal/SourceArt/OpenWorld/Models/models.json`).

## VFX flipbooks (CC0)

Flame, fireball, explosion and smoke flipbooks from Unity Labs' 2016 free VFX image sequences (CC0 1.0, no attribution
required), fetched by `unreal/Tools/vfx/fetch_cc0_flipbooks.py` (source and licence per file in the git-ignored
`unreal/SourceArt/VFX/Flipbooks/CC0/cc0_flipbooks.json`).

## Epic Games content (licensed for Unreal Engine projects)

Game Animation Sample motion and the MetaHuman body (migrated by `unreal/Tools/gasp/`), and the Niagara Examples Pack (added
from Fab by the owner) are used under the Unreal Engine EULA / Fab licence; they are not redistributed in this repository.
