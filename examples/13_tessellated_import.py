# The kitchen of example 03, imported EXACTLY.
#
# import_step_parts() rebuilds every solid as a formula and FITS its free-form (B-spline) faces with simple
# surfaces: fast and light, and an approximation (example 03 shows how far off, and draws the file's own
# surface where it matters).  import_step_tessellated_parts() is for parts that are almost all free-form, where
# that does not work: it fits nothing.  Every solid is tessellated straight from its trimmed faces, on all
# the processor's threads, and the triangles are made the exact signed distance field of the part.
#
# Look at the output: how many triangles, how many of the faces are free-form, and how long the tessellation
# took (the first run; it is kept in step/Keukencombinatie.stp.fieldes-tessellation and read from there next time).
# Every part is exact, and a shell, an offset or a lattice of one is made from its true distance:
# `hollow` below is the panel with 3 mm walls.
#
# The parts come in the same order, with the same names, units and bounds as example 03's: kitchen[7] is the
# same part in both.  What you give up is the formula: the faces of this import cannot be dragged (the gizmo
# of the model tree works), and a part that is mostly flat faces and cylinders is lighter as import_step_parts().
from fieldes import *

kitchen = import_step_tessellated_parts("step/Keukencombinatie.stp")
kitchen_0 = kitchen[0][0]
kitchen_0
kitchen_1 = kitchen[1][0]
kitchen_1
kitchen_2 = kitchen[2][0]
kitchen_2
kitchen_3 = kitchen[3][0]
kitchen_3
kitchen_4 = kitchen[4][0]
kitchen_4
kitchen_5 = kitchen[5][0]
kitchen_5
kitchen_6 = kitchen[6][0]
kitchen_6
kitchen_8 = kitchen[8][0]
kitchen_8
kitchen_9 = kitchen[9][0]
kitchen_9
kitchen_10 = kitchen[10][0]
kitchen_10
kitchen_11 = kitchen[11][0]
kitchen_11
kitchen_12 = kitchen[12][0]
kitchen_12
kitchen_13 = kitchen[13][0]
kitchen_13
kitchen_14 = kitchen[14][0]
kitchen_14
kitchen_15 = kitchen[15][0]
kitchen_15
kitchen_16 = kitchen[16][0]
kitchen_16
kitchen_17 = kitchen[17][0]
kitchen_17
kitchen_18 = kitchen[18][0]
kitchen_18
kitchen_19 = kitchen[19][0]
kitchen_19
kitchen_20 = kitchen[20][0]
kitchen_20
kitchen_21 = kitchen[21][0]
kitchen_21
kitchen_22 = kitchen[22][0]
kitchen_22
kitchen_23 = kitchen[23][0]
kitchen_23
kitchen_24 = kitchen[24][0]
kitchen_24
kitchen_25 = kitchen[25][0]
kitchen_25
kitchen_26 = kitchen[26][0]
kitchen_26
kitchen_27 = kitchen[27][0]
kitchen_27
kitchen_28 = kitchen[28][0]
kitchen_28
kitchen_29 = kitchen[29][0]
kitchen_29
kitchen_30 = kitchen[30][0]
kitchen_30
kitchen_31 = kitchen[31][0]
kitchen_31
kitchen_32 = kitchen[32][0]
kitchen_32
kitchen_33 = kitchen[33][0]
kitchen_33
kitchen_34 = kitchen[34][0]
kitchen_34
kitchen_35 = kitchen[35][0]
kitchen_35
kitchen_36 = kitchen[36][0]
kitchen_36
kitchen_37 = kitchen[37][0]
kitchen_37
kitchen_38 = kitchen[38][0]
kitchen_38
kitchen_39 = kitchen[39][0]
kitchen_39
kitchen_40 = kitchen[40][0]
kitchen_40
kitchen_41 = kitchen[41][0]
kitchen_41
kitchen_42 = kitchen[42][0]
kitchen_42
kitchen_43 = kitchen[43][0]
kitchen_43
kitchen_44 = kitchen[44][0]
kitchen_44
kitchen_45 = kitchen[45][0]
kitchen_45
kitchen_46 = kitchen[46][0]
kitchen_46
kitchen_47 = kitchen[47][0]
kitchen_47
kitchen_48 = kitchen[48][0]
kitchen_48
kitchen_49 = kitchen[49][0]
kitchen_49
kitchen_50 = kitchen[50][0]
kitchen_50
kitchen_51 = kitchen[51][0]
kitchen_51
kitchen_52 = kitchen[52][0]
kitchen_52
kitchen_53 = kitchen[53][0]
kitchen_53
kitchen_54 = kitchen[54][0]
kitchen_54
kitchen_55 = kitchen[55][0]
kitchen_55
kitchen_56 = kitchen[56][0]
kitchen_56
kitchen_57 = kitchen[57][0]
kitchen_57
kitchen_58 = kitchen[58][0]
kitchen_58
kitchen_59 = kitchen[59][0]
kitchen_59
kitchen_60 = kitchen[60][0]
kitchen_60
kitchen_61 = kitchen[61][0]
kitchen_61
kitchen_62 = kitchen[62][0]
kitchen_62
kitchen_63 = kitchen[63][0]
kitchen_63
kitchen_64 = kitchen[64][0]
kitchen_64
kitchen_65 = kitchen[65][0]
kitchen_65
kitchen_66 = kitchen[66][0]
kitchen_66
kitchen_67 = kitchen[67][0]
kitchen_67
kitchen_68 = kitchen[68][0]
kitchen_68
kitchen_69 = kitchen[69][0]
kitchen_69
kitchen_70 = kitchen[70][0]
kitchen_70
kitchen_71 = kitchen[71][0]
kitchen_71
kitchen_72 = kitchen[72][0]
kitchen_72
kitchen_73 = kitchen[73][0]
kitchen_73
kitchen_74 = kitchen[74][0]
kitchen_74
kitchen_75 = kitchen[75][0]
kitchen_75
kitchen_76 = kitchen[76][0]
kitchen_76
kitchen_77 = kitchen[77][0]
kitchen_77
kitchen_78 = kitchen[78][0]
kitchen_78
kitchen_79 = kitchen[79][0]
kitchen_79
kitchen_80 = kitchen[80][0]
kitchen_80
kitchen_81 = kitchen[81][0]
kitchen_81
kitchen_82 = kitchen[82][0]
kitchen_82
kitchen_83 = kitchen[83][0]
kitchen_83
kitchen_84 = kitchen[84][0]
kitchen_84
kitchen_85 = kitchen[85][0]
kitchen_85
kitchen_86 = kitchen[86][0]
kitchen_86
kitchen_87 = kitchen[87][0]
kitchen_87
kitchen_88 = kitchen[88][0]
kitchen_88
kitchen_89 = kitchen[89][0]
kitchen_89

panel = kitchen[7][0]                       # a curved panel
panel

view.set_bounds(*roi(kitchen))
view.set_resolution(roi_resolution(kitchen))        # every part at its own resolution
view.set_quality(8)

# hidden: [part for part, _ in kitchen]
