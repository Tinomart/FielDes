# Noise: one of every kind, and what scale, octaves, stretch and warp do to it.
#
#   row 1   the five kinds        perlin  ·  billow  ·  ridged  ·  cellular  ·  cracks
#   row 2   shaping them          stretched into streaks  ·  warped (marble)  ·  one octave  ·  six octaves  ·  a scale that is a field
#
# noise_field(scale, octaves, seed, gain, lacunarity, amplitude, kind, warp) is a FIELD: a number at every point of space, around zero.  A field is not
# a body, so nothing of it is drawn by itself -- each ball here is `ball - depth * noise`, which pushes the surface out where the noise is positive and
# in where it is negative, so the relief you see IS the noise.  Open the eye of a field in the model tree (n_perlin, n_billow ...) to look at the field
# itself: a plane coloured by it, with a Position slider.
#
#   scale       the size (mm) of the largest features -- or three sizes, (x, y, z), to stretch the pattern: (40, 4, 4) is streaks along x
#   octaves     how many layers of finer detail are added, each `lacunarity` (2) times finer and `gain` (0.5) as strong
#   seed        another pattern of the same kind
#   amplitude   how strong.  Perlin noise stays mostly inside +-0.3, so it is given an amplitude of 3 here to use the range -1 .. 1 that the
#               other kinds are made over
#   kind        'perlin' soft hills and hollows  ·  'billow' puffy humps with creases  ·  'ridged' sharp ridges (mountains, veins)
#               'cellular' the distance to the nearest of random points: pits in the middle of cells, walls between them (bubbles, scales)
#               'cracks' the distance from the walls between those cells: low along them (a web, a foam's walls, cracks)
#   warp        pushes the coordinates about by other noises, so the pattern bends (marble, flowing shapes); in feature sizes, 1 is a lot
#
# Where is what (x to the right, y away from you; the names are the names of the rows in the model tree -- click a row and its ball is lit up):
#
#   y = 40    streaks      marble       smooth_ball   rough_ball    graded
#   y =  0    perlin       billow       ridged        cellular      cracks
#             x = 0        x = 40       x = 80        x = 120       x = 160
#
# Balls are 30 mm across and 40 mm apart.  Change a number, or the kind, and the ball follows.  The finest features here are about 2 mm, so the resolution
# is 2 (samples per mm) and the picture is there in about ten seconds; at 3 the finest grain is sharper, and it takes a minute and a half.
from fieldes import *

view.set_bounds([-22, -22, -22], [182, 62, 22], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

# ---- row 1: the five kinds, each at a feature size of 8 mm ---------------------------------------------------------------------------------
n_perlin = noise_field(8, octaves=3, seed=1, amplitude=3)
perlin = sphere(15, (0, 0, 0)) - 1.4 * n_perlin
perlin

n_billow = noise_field(8, octaves=3, seed=1, kind='billow')
billow = sphere(15, (40, 0, 0)) - 1.4 * n_billow
billow

n_ridged = noise_field(8, octaves=3, seed=1, kind='ridged')
ridged = sphere(15, (80, 0, 0)) - 1.4 * n_ridged
ridged

n_cellular = noise_field(8, octaves=1, seed=1, kind='cellular')
cellular = sphere(15, (120, 0, 0)) - 1.4 * n_cellular
cellular

n_cracks = noise_field(8, octaves=1, seed=1, kind='cracks')
cracks = sphere(15, (160, 0, 0)) - 1.4 * n_cracks
cracks

# ---- row 2: shaping them -----------------------------------------------------------------------------------------------------------------------
# Streaks: long in x (40 mm), narrow in y and z (4 mm).
n_streaks = noise_field((40, 4, 4), octaves=2, seed=3, kind='ridged')
streaks = sphere(15, (0, 40, 0)) - 1.4 * n_streaks
streaks

# Marble: a ridged noise whose coordinates are pushed about by three other noises.
n_marble = noise_field(10, octaves=3, seed=2, kind='ridged', warp=1.0)
marble = sphere(15, (40, 40, 0)) - 1.4 * n_marble
marble

# The same Perlin noise with one layer (smooth) and with six (rough): each extra octave adds finer detail, half as strong.
n_smooth = noise_field(10, octaves=1, seed=4, amplitude=3)
smooth_ball = sphere(15, (80, 40, 0)) - 1.4 * n_smooth
smooth_ball

n_rough = noise_field(10, octaves=6, seed=4, amplitude=3)
rough_ball = sphere(15, (120, 40, 0)) - 1.4 * n_rough
rough_ball

# A scale that is a field: wherever a number goes, a field goes.  The features are 3 mm at the left of this ball and 12 mm at the right.
feature = ramp(x_field(), (145, 175), (3.0, 12.0))
n_graded = noise_field(feature, octaves=2, seed=5, kind='billow')
graded = sphere(15, (160, 40, 0)) - 1.4 * n_graded
graded
