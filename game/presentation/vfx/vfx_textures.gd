class_name VfxTextures
extends RefCounted
## Procedural textures shared by every VFX shader. Everything is generated in code
## (no imported assets), cached in static variables and built lazily on first use.
##
## Call VfxTextures.warm() once during loading to move the (~0.3 s) generation cost
## off the first gameplay frame.

const VOLUME_SIZE: int = 32
const NOISE2D_SIZE: int = 128

static var _volume: ImageTexture3D = null
static var _noise2d: ImageTexture = null


## Pre-generate all shared textures (call from a loading screen).
static func warm() -> void:
	noise_volume()
	noise_2d()


## RGBA8 3D smooth-noise volume (32^3, ~0.4 MB), sampled with mirrored coordinates in the shaders
## (no hard repeat seams). All channels are smooth fbm at different frequencies:
##   R = low frequency tonal variation        G = mid frequency (domain warp / wobble)
##   B = mid-high frequency (domain warp)     A = fine grain
## Crack networks are analytic Voronoi in the shaders, not baked here (crisp at any distance).
static func noise_volume() -> ImageTexture3D:
	if _volume != null:
		return _volume
	var n: int = VOLUME_SIZE
	var freqs: Array[float] = [0.04, 0.065, 0.09, 0.14]
	var octaves: Array[int] = [4, 3, 3, 2]
	var chans: Array = []
	for c in 4:
		var nz := FastNoiseLite.new()
		nz.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
		nz.fractal_type = FastNoiseLite.FRACTAL_FBM
		nz.fractal_octaves = octaves[c]
		nz.frequency = freqs[c]
		nz.seed = 11 + c * 17
		chans.append(nz.get_image_3d(n, n, n, false, true))
	var slices: Array[Image] = []
	var plane: int = n * n
	for z in n:
		var d0: PackedByteArray = (chans[0][z] as Image).get_data()
		var d1: PackedByteArray = (chans[1][z] as Image).get_data()
		var d2: PackedByteArray = (chans[2][z] as Image).get_data()
		var d3: PackedByteArray = (chans[3][z] as Image).get_data()
		var out := PackedByteArray()
		out.resize(plane * 4)
		var o: int = 0
		for i in plane:
			out[o] = d0[i]
			out[o + 1] = d1[i]
			out[o + 2] = d2[i]
			out[o + 3] = d3[i]
			o += 4
		slices.append(Image.create_from_data(n, n, false, Image.FORMAT_RGBA8, out))
	var tex := ImageTexture3D.new()
	tex.create(Image.FORMAT_RGBA8, n, n, n, false, slices)
	_volume = tex
	return _volume


## Tileable RGBA8 2D noise: R = fbm, G = ridged-ish streak noise, B = cellular puff, A = fine grain.
static func noise_2d() -> ImageTexture:
	if _noise2d != null:
		return _noise2d
	var s: int = NOISE2D_SIZE
	var a := FastNoiseLite.new()
	a.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	a.fractal_type = FastNoiseLite.FRACTAL_FBM
	a.fractal_octaves = 4
	a.frequency = 0.03
	a.seed = 3
	var b := FastNoiseLite.new()
	b.noise_type = FastNoiseLite.TYPE_PERLIN
	b.fractal_type = FastNoiseLite.FRACTAL_RIDGED
	b.fractal_octaves = 3
	b.frequency = 0.05
	b.seed = 5
	var c := FastNoiseLite.new()
	c.noise_type = FastNoiseLite.TYPE_CELLULAR
	c.cellular_return_type = FastNoiseLite.RETURN_DISTANCE
	c.frequency = 0.05
	c.seed = 9
	var d := FastNoiseLite.new()
	d.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	d.frequency = 0.18
	d.seed = 13
	var ia: Image = a.get_seamless_image(s, s, false, false, 0.25, true)
	var ib: Image = b.get_seamless_image(s, s, false, false, 0.25, true)
	var ic: Image = c.get_seamless_image(s, s, false, false, 0.25, true)
	var id: Image = d.get_seamless_image(s, s, false, false, 0.25, true)
	var da: PackedByteArray = ia.get_data()
	var db: PackedByteArray = ib.get_data()
	var dc: PackedByteArray = ic.get_data()
	var dd: PackedByteArray = id.get_data()
	var out := PackedByteArray()
	out.resize(s * s * 4)
	var o: int = 0
	for i in s * s:
		out[o] = da[i]
		out[o + 1] = db[i]
		out[o + 2] = dc[i]
		out[o + 3] = dd[i]
		o += 4
	var img := Image.create_from_data(s, s, false, Image.FORMAT_RGBA8, out)
	img.generate_mipmaps()
	_noise2d = ImageTexture.create_from_image(img)
	return _noise2d
