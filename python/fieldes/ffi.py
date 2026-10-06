'''
Python bindings to the libfive CAD kernel
Copyright (C) 2021  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
'''

import ctypes
import os
import sys

def try_link(folder, name):
    if sys.platform == "linux" or sys.platform == "linux2":
        suffix = '.so'
    elif sys.platform == "darwin":
        suffix = '.dylib'
    elif sys.platform == "win32":
        suffix = '.dll'
    path = os.path.join(folder, name + suffix)
    try:
        return ctypes.cdll.LoadLibrary(path)
    except OSError:
        return None

def paths_for(folder):
    # Where the libraries are: the folder the application names (FIELDES_DIR), the
    # folders from the package up (a packaged application keeps the package next to
    # the libraries), then wherever the system looks.
    paths = []
    env = os.environ.get('FIELDES_DIR')
    if env:
        paths.append(env)
    here = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
        paths.append(here)
        here = os.path.dirname(here)
    paths.append("")
    return paths

def link_lib(folder, name):
    for p in paths_for(folder):
        lib = try_link(p, name)
        if lib is not None:
            return lib
    raise RuntimeError("Could not find {} library".format(name))

lib = link_lib('src', 'fieldes')
stdlib = link_lib('stdlib', 'fieldes-stdlib')

################################################################################

class libfive_interval_t(ctypes.Structure):
    _fields_ = [("lower", ctypes.c_float), ("upper", ctypes.c_float)]
class libfive_region_t(ctypes.Structure):
    _fields_ = [("X", libfive_interval_t),
                ("Y", libfive_interval_t),
                ("Z", libfive_interval_t)]
class libfive_vec3_t(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float),
                ("y", ctypes.c_float),
                ("z", ctypes.c_float)]

libfive_tree = ctypes.c_void_p

class libfive_tri_t(ctypes.Structure):
    _fields_ = [("a", ctypes.c_uint32),
                ("b", ctypes.c_uint32),
                ("c", ctypes.c_uint32)]

class libfive_mesh_t(ctypes.Structure):
    _fields_ = [("verts", ctypes.POINTER(libfive_vec3_t)),
                ("tris",  ctypes.POINTER(libfive_tri_t)),
                ("tri_count", ctypes.c_uint32),
                ("vert_count", ctypes.c_uint32)]

################################################################################

# Types used in the libfive stdlib
class tvec2(ctypes.Structure):
    _fields_ = [("x", libfive_tree), ("y", libfive_tree)]
    def __init__(self, x, y):
        super().__init__(x, y)

class tvec3(ctypes.Structure):
    _fields_ = [("x", libfive_tree), ("y", libfive_tree), ("z", libfive_tree)]
    def __init__(self, x, y, z):
        super().__init__(x, y, z)
tfloat = libfive_tree

################################################################################
# Function signatures
lib.libfive_tree_delete.argtypes = [libfive_tree]

lib.libfive_tree_const.argtypes = [ctypes.c_float]
lib.libfive_tree_const.restype = libfive_tree

lib.libfive_opcode_enum.argtypes = [ctypes.c_char_p]
lib.libfive_opcode_enum.restype = ctypes.c_int

lib.libfive_tree_is_var.argtypes = [libfive_tree]
lib.libfive_tree_is_var.restype = ctypes.c_uint8

lib.libfive_opcode_args.argtypes = [ctypes.c_int]
lib.libfive_opcode_args.restype = ctypes.c_int

lib.libfive_tree_nullary.argtypes = [ctypes.c_int]
lib.libfive_tree_nullary.restype = libfive_tree

lib.libfive_tree_unary.argtypes = [ctypes.c_int, libfive_tree]
lib.libfive_tree_unary.restype = libfive_tree

lib.libfive_tree_binary.argtypes = [ctypes.c_int, libfive_tree, libfive_tree]
lib.libfive_tree_binary.restype = libfive_tree

lib.libfive_tree_id.argtypes = [libfive_tree]
lib.libfive_tree_id.restype = ctypes.c_void_p

lib.libfive_tree_remap.argtypes = [libfive_tree, libfive_tree, libfive_tree, libfive_tree]
lib.libfive_tree_remap.restype = libfive_tree

lib.libfive_tree_print.argtypes = [libfive_tree]
lib.libfive_tree_print.restype = ctypes.c_void_p # actually a c_char_p,
# but we don't want Python to auto-convert into a bytestring

lib.libfive_free_str.argtypes = [ctypes.c_char_p]

try:
    # The numbers of a tree that place its surfaces, as variables (fieldes.stdlib.handles.expose)
    lib.libfive_tree_expose_count.argtypes = [libfive_tree]
    lib.libfive_tree_expose_count.restype = ctypes.c_int
    lib.libfive_tree_expose_values.argtypes = [libfive_tree, ctypes.POINTER(ctypes.c_float)]
    lib.libfive_tree_expose_values.restype = None
    lib.libfive_tree_expose.argtypes = [libfive_tree, ctypes.POINTER(libfive_tree), ctypes.c_int]
    lib.libfive_tree_expose.restype = libfive_tree
except AttributeError:
    pass

try:
    # The key the render cache keeps a shape's mesh by
    lib.libfive_tree_persistent_key.argtypes = [libfive_tree, ctypes.POINTER(libfive_tree), ctypes.POINTER(ctypes.c_float),
                                                ctypes.c_int]
    lib.libfive_tree_persistent_key.restype = ctypes.c_void_p
except AttributeError:
    pass

try:
    # An exact key of a tree's structure (a library from before it has none: nothing is then cached by content)
    lib.libfive_tree_content_key.argtypes = [libfive_tree]
    lib.libfive_tree_content_key.restype = ctypes.c_void_p
except AttributeError:
    pass

lib.libfive_tree_save_mesh.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float, ctypes.c_char_p]
lib.libfive_tree_save_mesh.restype = ctypes.c_uint8

lib.libfive_tree_save_meshes.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float, ctypes.c_float, ctypes.c_char_p]
lib.libfive_tree_save_meshes.restype = ctypes.c_uint8

lib.libfive_tree_save.argtypes = [libfive_tree, ctypes.c_char_p]
lib.libfive_tree_save.restype = ctypes.c_bool

if hasattr(lib, 'libfive_tree_can_save'):
    lib.libfive_tree_can_save.argtypes = [libfive_tree]
    lib.libfive_tree_can_save.restype = ctypes.c_bool
if hasattr(lib, 'libfive_tree_axes'):
    lib.libfive_tree_axes.argtypes = [libfive_tree]
    lib.libfive_tree_axes.restype = ctypes.c_int

lib.libfive_tree_load.argtypes = [ctypes.c_char_p]
lib.libfive_tree_load.restype = libfive_tree

lib.libfive_import_step_last_message.argtypes = []
lib.libfive_import_step_last_message.restype = ctypes.c_char_p

class libfive_step_part_t(ctypes.Structure):
    _fields_ = [("tree", libfive_tree),
                ("bounds", libfive_region_t),
                ("error", ctypes.c_char_p),
                ("name", ctypes.c_char_p),
                ("marker", libfive_tree),
                ("solid", ctypes.c_int32),
                ("instance", ctypes.c_int32),
                ("detail", ctypes.c_double),
                ("area_flat", ctypes.c_double),
                ("area_curved", ctypes.c_double)]
class libfive_step_parts_t(ctypes.Structure):
    _fields_ = [("parts", ctypes.POINTER(libfive_step_part_t)),
                ("count", ctypes.c_uint32)]

lib.libfive_step_parts_delete.argtypes = [ctypes.POINTER(libfive_step_parts_t)]
lib.libfive_step_parts_delete.restype = None

try:
    lib.libfive_step_import_version.argtypes = []
    lib.libfive_step_import_version.restype = ctypes.c_int
except AttributeError:
    pass    # an older library: the import cache then follows the library file's hash

lib.libfive_import_step_parts_reconstructed.argtypes = [ctypes.c_char_p]
lib.libfive_import_step_parts_reconstructed.restype = ctypes.POINTER(libfive_step_parts_t)

try:
    lib.libfive_step_exact_surface.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int,
                                               ctypes.POINTER(ctypes.c_double), ctypes.c_int]
    lib.libfive_step_exact_surface.restype = ctypes.POINTER(libfive_mesh_t)
    lib.libfive_tree_interval_lower.argtypes = [libfive_tree, libfive_region_t,
                                                ctypes.POINTER(libfive_tree), ctypes.POINTER(ctypes.c_float),
                                                ctypes.c_int]
    lib.libfive_tree_interval_lower.restype = ctypes.c_float
except AttributeError:
    pass    # an older library

class libfive_step_brep_solid_t(ctypes.Structure):
    _fields_ = [("mesh", ctypes.POINTER(libfive_mesh_t)),
                ("error", ctypes.c_char_p),
                ("faces", ctypes.c_int32),
                ("bspline_faces", ctypes.c_int32)]
class libfive_step_brep_instance_t(ctypes.Structure):
    _fields_ = [("solid", ctypes.c_int32),
                ("instance", ctypes.c_int32),
                ("linear", ctypes.c_double * 9),
                ("offset", ctypes.c_double * 3),
                ("name", ctypes.c_char_p),
                ("bounds", libfive_region_t),
                ("detail", ctypes.c_double),
                ("area_flat", ctypes.c_double),
                ("area_curved", ctypes.c_double)]
class libfive_step_brep_t(ctypes.Structure):
    _fields_ = [("solids", ctypes.POINTER(libfive_step_brep_solid_t)),
                ("solid_count", ctypes.c_uint32),
                ("instances", ctypes.POINTER(libfive_step_brep_instance_t)),
                ("instance_count", ctypes.c_uint32)]

try:
    # The tessellated import (fieldes.stdlib.cad_import.import_step_tessellated_parts)
    lib.libfive_step_brep_read.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.libfive_step_brep_read.restype = ctypes.POINTER(libfive_step_brep_t)
    lib.libfive_step_brep_delete.argtypes = [ctypes.POINTER(libfive_step_brep_t)]
    lib.libfive_step_brep_delete.restype = None
    lib.libfive_step_tessellation_version.argtypes = []
    lib.libfive_step_tessellation_version.restype = ctypes.c_int
except AttributeError:
    pass    # an older library: no tessellated import

class libfive_mesh_import_info_t(ctypes.Structure):
    _fields_ = [("triangles", ctypes.c_uint32),
                ("vertices", ctypes.c_uint32),
                ("components", ctypes.c_uint32),
                ("boundary_edges", ctypes.c_uint32),
                ("nonmanifold_edges", ctypes.c_uint32),
                ("reoriented", ctypes.c_uint32),
                ("dropped", ctypes.c_uint32),
                ("watertight", ctypes.c_uint8),
                ("winding_sign", ctypes.c_uint8),
                ("bounds", libfive_region_t)]

lib.libfive_import_mesh.argtypes = [ctypes.c_char_p, ctypes.c_float,
                                    ctypes.POINTER(libfive_mesh_import_info_t)]
lib.libfive_import_mesh.restype = libfive_tree

lib.libfive_mesh_from_arrays.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_uint32,
                                         ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint32,
                                         ctypes.c_float,
                                         ctypes.POINTER(libfive_mesh_import_info_t)]
lib.libfive_mesh_from_arrays.restype = libfive_tree

try:
    lib.libfive_mesh_flood.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_uint32,
                                       ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint32,
                                       ctypes.POINTER(ctypes.c_float), ctypes.c_float, ctypes.c_int,
                                       ctypes.c_float, ctypes.POINTER(ctypes.c_uint8),
                                       ctypes.POINTER(ctypes.c_float)]
    lib.libfive_mesh_flood.restype = ctypes.c_int64
    lib.libfive_mesh_patch.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_uint32,
                                       ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint32,
                                       ctypes.POINTER(ctypes.c_uint8),
                                       ctypes.POINTER(libfive_mesh_import_info_t)]
    lib.libfive_mesh_patch.restype = libfive_tree
except AttributeError:
    pass    # an older library: no surface selection

lib.libfive_import_mesh_last_message.argtypes = []
lib.libfive_import_mesh_last_message.restype = ctypes.c_char_p

lib.libfive_tree_eval_f.argtypes = [libfive_tree, libfive_vec3_t]
lib.libfive_tree_eval_f.restype = ctypes.c_float

lib.libfive_tree_eval_r.argtypes = [libfive_tree, libfive_region_t]
lib.libfive_tree_eval_r.restype = libfive_interval_t

lib.libfive_tree_bounds.argtypes = [libfive_tree, libfive_region_t, ctypes.c_int, ctypes.c_float,
                                   ctypes.POINTER(libfive_region_t), ctypes.POINTER(ctypes.c_int)]
lib.libfive_tree_bounds.restype = ctypes.c_bool
try:
    lib.libfive_tree_bounds_vars.argtypes = [libfive_tree, libfive_region_t, ctypes.c_int, ctypes.c_float,
                                             ctypes.POINTER(libfive_region_t), ctypes.POINTER(ctypes.c_int),
                                             ctypes.POINTER(libfive_tree), ctypes.POINTER(ctypes.c_float),
                                             ctypes.c_int]
    lib.libfive_tree_bounds_vars.restype = ctypes.c_bool
except AttributeError:
    pass    # an older library: the numbers of var()s are read as 0

lib.libfive_tree_eval_points.argtypes = [libfive_tree, ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_float)]
lib.libfive_tree_eval_points.restype = None
try:
    lib.libfive_tree_eval_points_vars.argtypes = [libfive_tree, ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                                  ctypes.POINTER(ctypes.c_float),
                                                  ctypes.POINTER(libfive_tree), ctypes.POINTER(ctypes.c_float),
                                                  ctypes.c_int]
    lib.libfive_tree_eval_points_vars.restype = None
except AttributeError:
    pass    # an older library: the numbers of var()s are read as 0

lib.libfive_tree_copy.argtypes = [libfive_tree]
lib.libfive_tree_copy.restype = libfive_tree

# A script run's progress (run_progress.py)
lib.libfive_run_begin.argtypes = [ctypes.c_int]
lib.libfive_run_begin.restype = None
lib.libfive_run_step.argtypes = [ctypes.c_int, ctypes.c_char_p]
lib.libfive_run_step.restype = None
lib.libfive_run_end.argtypes = []
lib.libfive_run_end.restype = None
lib.libfive_run_task_begin.argtypes = [ctypes.c_char_p]
lib.libfive_run_task_begin.restype = None
lib.libfive_run_task_set.argtypes = [ctypes.c_double, ctypes.c_char_p]
lib.libfive_run_task_set.restype = None
lib.libfive_run_task_span.argtypes = [ctypes.c_double, ctypes.c_double]
lib.libfive_run_task_span.restype = None
lib.libfive_run_task_end.argtypes = []
lib.libfive_run_task_end.restype = None

libfive_fea_p = ctypes.c_void_p
lib.libfive_fea_new.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float, ctypes.c_float, ctypes.c_float]
lib.libfive_fea_new.restype = libfive_fea_p
lib.libfive_fea_add_support.argtypes = [libfive_fea_p, libfive_tree, ctypes.c_int, ctypes.c_int, ctypes.c_int]
lib.libfive_fea_add_support.restype = None
lib.libfive_fea_add_force.argtypes = [libfive_fea_p, libfive_tree, ctypes.c_float, ctypes.c_float, ctypes.c_float]
lib.libfive_fea_add_force.restype = None
lib.libfive_fea_add_force_case.argtypes = [libfive_fea_p, libfive_tree, ctypes.c_float, ctypes.c_float,
                                           ctypes.c_float, ctypes.c_int]
lib.libfive_fea_add_force_case.restype = None
try:
    lib.libfive_fea_set_element.argtypes = [libfive_fea_p, ctypes.c_int]
    lib.libfive_fea_set_element.restype = None
except AttributeError:
    pass    # an older library: hexahedra with incompatible modes only
lib.libfive_fea_set_gravity.argtypes = [libfive_fea_p, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float]
lib.libfive_fea_set_gravity.restype = None
lib.libfive_fea_set_thermal.argtypes = [libfive_fea_p, libfive_tree, ctypes.c_float, ctypes.c_float]
lib.libfive_fea_set_thermal.restype = None
lib.libfive_fea_modal.argtypes = [libfive_fea_p, ctypes.c_int, ctypes.c_float, ctypes.c_int, ctypes.c_float]
lib.libfive_fea_modal.restype = ctypes.c_int
lib.libfive_fea_mode_count.argtypes = [libfive_fea_p]
lib.libfive_fea_mode_count.restype = ctypes.c_int
lib.libfive_fea_mode_frequency.argtypes = [libfive_fea_p, ctypes.c_int]
lib.libfive_fea_mode_frequency.restype = ctypes.c_double
lib.libfive_fea_mode_field.argtypes = [libfive_fea_p, ctypes.c_int, ctypes.c_int]
lib.libfive_fea_mode_field.restype = libfive_tree
lib.libfive_fea_mode_field_min.argtypes = [libfive_fea_p, ctypes.c_int, ctypes.c_int]
lib.libfive_fea_mode_field_min.restype = ctypes.c_float
lib.libfive_fea_mode_field_max.argtypes = [libfive_fea_p, ctypes.c_int, ctypes.c_int]
lib.libfive_fea_mode_field_max.restype = ctypes.c_float
lib.libfive_fea_prepare.argtypes = [libfive_fea_p]
lib.libfive_fea_prepare.restype = ctypes.c_int
lib.libfive_fea_hash.argtypes = [libfive_fea_p]
lib.libfive_fea_hash.restype = ctypes.c_uint64
lib.libfive_fea_save.argtypes = [libfive_fea_p, ctypes.c_char_p]
lib.libfive_fea_save.restype = ctypes.c_int
lib.libfive_fea_load.argtypes = [ctypes.c_char_p]
lib.libfive_fea_load.restype = libfive_fea_p
lib.libfive_fea_set_salt.argtypes = [libfive_fea_p, ctypes.c_uint64]
lib.libfive_fea_set_salt.restype = None
lib.libfive_fea_solve.argtypes = [libfive_fea_p, ctypes.c_int, ctypes.c_float]
lib.libfive_fea_solve.restype = ctypes.c_int
lib.libfive_fea_message.argtypes = [libfive_fea_p]
lib.libfive_fea_message.restype = ctypes.c_char_p
lib.libfive_fea_field.argtypes = [libfive_fea_p, ctypes.c_int]
lib.libfive_fea_field.restype = libfive_tree
lib.libfive_fea_field_min.argtypes = [libfive_fea_p, ctypes.c_int]
lib.libfive_fea_field_min.restype = ctypes.c_float
lib.libfive_fea_field_max.argtypes = [libfive_fea_p, ctypes.c_int]
lib.libfive_fea_field_max.restype = ctypes.c_float
lib.libfive_fea_stat.argtypes = [libfive_fea_p, ctypes.c_int]
lib.libfive_fea_stat.restype = ctypes.c_double
lib.libfive_fea_grid.argtypes = [libfive_fea_p, ctypes.POINTER(ctypes.c_double),
                                 ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_int)]
lib.libfive_fea_grid.restype = ctypes.c_int
lib.libfive_fea_elements.argtypes = [libfive_fea_p, ctypes.POINTER(ctypes.c_float)]
lib.libfive_fea_elements.restype = None
try:
    lib.libfive_fea_element.argtypes = [libfive_fea_p]
    lib.libfive_fea_element.restype = ctypes.c_int
    lib.libfive_fea_elements_per_cell.argtypes = [libfive_fea_p]
    lib.libfive_fea_elements_per_cell.restype = ctypes.c_int
    lib.libfive_fea_element_values.argtypes = [libfive_fea_p, ctypes.c_int, ctypes.POINTER(ctypes.c_float),
                                               ctypes.c_int64]
    lib.libfive_fea_element_values.restype = ctypes.c_int64
    lib.libfive_fea_element_range.argtypes = [libfive_fea_p, ctypes.c_int, ctypes.POINTER(ctypes.c_float),
                                              ctypes.POINTER(ctypes.c_float)]
    lib.libfive_fea_element_range.restype = ctypes.c_int
except AttributeError:
    pass    # an older library: no per-element values
lib.libfive_fea_delete.argtypes = [libfive_fea_p]
lib.libfive_fea_delete.restype = None

libfive_tetmesh_p = ctypes.c_void_p
try:
    lib.libfive_tetmesh_new.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float]
    lib.libfive_tetmesh_new.restype = libfive_tetmesh_p
    lib.libfive_tetmesh_last_message.argtypes = []
    lib.libfive_tetmesh_last_message.restype = ctypes.c_char_p
    lib.libfive_tetmesh_counts.argtypes = [libfive_tetmesh_p, ctypes.POINTER(ctypes.c_int64)]
    lib.libfive_tetmesh_counts.restype = None
    lib.libfive_tetmesh_vertices.argtypes = [libfive_tetmesh_p, ctypes.POINTER(ctypes.c_double)]
    lib.libfive_tetmesh_vertices.restype = None
    lib.libfive_tetmesh_tets.argtypes = [libfive_tetmesh_p, ctypes.POINTER(ctypes.c_int32)]
    lib.libfive_tetmesh_tets.restype = None
    lib.libfive_tetmesh_faces.argtypes = [libfive_tetmesh_p, ctypes.POINTER(ctypes.c_int32)]
    lib.libfive_tetmesh_faces.restype = None
    lib.libfive_tetmesh_quality.argtypes = [libfive_tetmesh_p, ctypes.POINTER(ctypes.c_double)]
    lib.libfive_tetmesh_quality.restype = None
    lib.libfive_tetmesh_delete.argtypes = [libfive_tetmesh_p]
    lib.libfive_tetmesh_delete.restype = None
    libfive_tetfea_p = ctypes.c_void_p
    lib.libfive_tetfea_new.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float, ctypes.c_float, ctypes.c_float]
    lib.libfive_tetfea_new.restype = libfive_tetfea_p
    lib.libfive_tetfea_add_support.argtypes = [libfive_tetfea_p, libfive_tree, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.libfive_tetfea_add_support.restype = None
    lib.libfive_tetfea_add_force.argtypes = [libfive_tetfea_p, libfive_tree, ctypes.c_float, ctypes.c_float,
                                             ctypes.c_float]
    lib.libfive_tetfea_add_force.restype = None
    lib.libfive_tetfea_set_gravity.argtypes = [libfive_tetfea_p, ctypes.c_float, ctypes.c_float, ctypes.c_float,
                                               ctypes.c_float]
    lib.libfive_tetfea_set_gravity.restype = None
    lib.libfive_tetfea_set_thermal.argtypes = [libfive_tetfea_p, libfive_tree, ctypes.c_float, ctypes.c_float]
    lib.libfive_tetfea_set_thermal.restype = None
    for name in ('stiffness', 'density', 'expansion'):
        fn = getattr(lib, 'libfive_tetfea_set_%s_field' % name, None)
        if fn is not None:
            fn.argtypes = [libfive_tetfea_p, libfive_tree]
            fn.restype = None
    if getattr(lib, 'libfive_tetfea_add_force_profile', None) is not None:
        lib.libfive_tetfea_add_force_profile.argtypes = [libfive_tetfea_p, libfive_tree, ctypes.c_float, ctypes.c_float,
                                                        ctypes.c_float, ctypes.c_int, libfive_tree]
        lib.libfive_tetfea_add_force_profile.restype = None
    lib.libfive_tetfea_prepare.argtypes = [libfive_tetfea_p]
    lib.libfive_tetfea_prepare.restype = ctypes.c_int
    lib.libfive_tetfea_hash.argtypes = [libfive_tetfea_p]
    lib.libfive_tetfea_hash.restype = ctypes.c_uint64
    lib.libfive_tetfea_save.argtypes = [libfive_tetfea_p, ctypes.c_char_p]
    lib.libfive_tetfea_save.restype = ctypes.c_int
    lib.libfive_tetfea_load.argtypes = [ctypes.c_char_p]
    lib.libfive_tetfea_load.restype = libfive_tetfea_p
    lib.libfive_tetfea_set_salt.argtypes = [libfive_tetfea_p, ctypes.c_uint64]
    lib.libfive_tetfea_set_salt.restype = None
    lib.libfive_tetfea_solve.argtypes = [libfive_tetfea_p, ctypes.c_int, ctypes.c_float]
    lib.libfive_tetfea_solve.restype = ctypes.c_int
    lib.libfive_tetfea_message.argtypes = [libfive_tetfea_p]
    lib.libfive_tetfea_message.restype = ctypes.c_char_p
    lib.libfive_tetfea_field.argtypes = [libfive_tetfea_p, ctypes.c_int]
    lib.libfive_tetfea_field.restype = libfive_tree
    lib.libfive_tetfea_field_min.argtypes = [libfive_tetfea_p, ctypes.c_int]
    lib.libfive_tetfea_field_min.restype = ctypes.c_float
    lib.libfive_tetfea_field_max.argtypes = [libfive_tetfea_p, ctypes.c_int]
    lib.libfive_tetfea_field_max.restype = ctypes.c_float
    lib.libfive_tetfea_stat.argtypes = [libfive_tetfea_p, ctypes.c_int]
    lib.libfive_tetfea_stat.restype = ctypes.c_double
    lib.libfive_tetfea_counts.argtypes = [libfive_tetfea_p, ctypes.POINTER(ctypes.c_int64)]
    lib.libfive_tetfea_counts.restype = None
    lib.libfive_tetfea_mesh.argtypes = [libfive_tetfea_p, ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_int32),
                                        ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32)]
    lib.libfive_tetfea_mesh.restype = None
    lib.libfive_tetfea_node_displacements.argtypes = [libfive_tetfea_p, ctypes.POINTER(ctypes.c_float)]
    lib.libfive_tetfea_node_displacements.restype = None
    lib.libfive_tetfea_element_values.argtypes = [libfive_tetfea_p, ctypes.c_int, ctypes.POINTER(ctypes.c_float),
                                                  ctypes.c_int64]
    lib.libfive_tetfea_element_values.restype = ctypes.c_int64
    lib.libfive_tetfea_element_range.argtypes = [libfive_tetfea_p, ctypes.c_int, ctypes.POINTER(ctypes.c_float),
                                                 ctypes.POINTER(ctypes.c_float)]
    lib.libfive_tetfea_element_range.restype = ctypes.c_int
    lib.libfive_tetfea_delete.argtypes = [libfive_tetfea_p]
    lib.libfive_tetfea_delete.restype = None
    lib.libfive_tetfea_add_force_case.argtypes = [libfive_tetfea_p, libfive_tree, ctypes.c_float, ctypes.c_float,
                                                  ctypes.c_float, ctypes.c_int]
    lib.libfive_tetfea_add_force_case.restype = None
    lib.libfive_tetfea_optimize.argtypes = [libfive_tetfea_p, ctypes.c_float, ctypes.c_float, ctypes.c_float,
                                            ctypes.c_int, ctypes.c_float, ctypes.POINTER(ctypes.c_void_p), ctypes.c_int,
                                            ctypes.POINTER(ctypes.c_void_p), ctypes.c_int, ctypes.c_int, ctypes.c_float,
                                            ctypes.c_int]
    lib.libfive_tetfea_optimize.restype = ctypes.c_int
    lib.libfive_tetfea_density.argtypes = [libfive_tetfea_p]
    lib.libfive_tetfea_density.restype = libfive_tree
    lib.libfive_tetfea_history.argtypes = [libfive_tetfea_p, ctypes.POINTER(ctypes.c_double), ctypes.c_int]
    lib.libfive_tetfea_history.restype = ctypes.c_int
    lib.libfive_tetfea_density_at.argtypes = [libfive_tetfea_p, ctypes.c_int]
    lib.libfive_tetfea_density_at.restype = libfive_tree
    lib.libfive_tetfea_pieces.argtypes = [libfive_tetfea_p, ctypes.c_double, ctypes.c_double]
    lib.libfive_tetfea_pieces.restype = ctypes.c_int
    lib.libfive_tetfea_modal.argtypes = [libfive_tetfea_p, ctypes.c_int, ctypes.c_float, ctypes.c_int, ctypes.c_float]
    lib.libfive_tetfea_modal.restype = ctypes.c_int
    lib.libfive_tetfea_mode_count.argtypes = [libfive_tetfea_p]
    lib.libfive_tetfea_mode_count.restype = ctypes.c_int
    lib.libfive_tetfea_mode_frequency.argtypes = [libfive_tetfea_p, ctypes.c_int]
    lib.libfive_tetfea_mode_frequency.restype = ctypes.c_double
    lib.libfive_tetfea_mode_field.argtypes = [libfive_tetfea_p, ctypes.c_int, ctypes.c_int]
    lib.libfive_tetfea_mode_field.restype = libfive_tree
    lib.libfive_tetfea_mode_field_min.argtypes = [libfive_tetfea_p, ctypes.c_int, ctypes.c_int]
    lib.libfive_tetfea_mode_field_min.restype = ctypes.c_float
    lib.libfive_tetfea_mode_field_max.argtypes = [libfive_tetfea_p, ctypes.c_int, ctypes.c_int]
    lib.libfive_tetfea_mode_field_max.restype = ctypes.c_float

    libfive_tetthermal_p = ctypes.c_void_p
    lib.libfive_tetthermal_new.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float, ctypes.c_float]
    lib.libfive_tetthermal_new.restype = libfive_tetthermal_p
    lib.libfive_tetthermal_add_temperature.argtypes = [libfive_tetthermal_p, libfive_tree, ctypes.c_float]
    lib.libfive_tetthermal_add_temperature.restype = None
    lib.libfive_tetthermal_add_heat.argtypes = [libfive_tetthermal_p, libfive_tree, ctypes.c_float]
    lib.libfive_tetthermal_add_heat.restype = None
    lib.libfive_tetthermal_add_generation.argtypes = [libfive_tetthermal_p, libfive_tree, ctypes.c_float]
    lib.libfive_tetthermal_add_generation.restype = None
    lib.libfive_tetthermal_add_convection.argtypes = [libfive_tetthermal_p, libfive_tree, ctypes.c_float, ctypes.c_float]
    lib.libfive_tetthermal_add_convection.restype = None
    if getattr(lib, 'libfive_tetthermal_set_conductivity_field', None) is not None:
        lib.libfive_tetthermal_set_conductivity_field.argtypes = [libfive_tetthermal_p, libfive_tree]
        lib.libfive_tetthermal_set_conductivity_field.restype = None
        lib.libfive_tetthermal_add_temperature_field.argtypes = [libfive_tetthermal_p, libfive_tree, ctypes.c_float,
                                                                libfive_tree]
        lib.libfive_tetthermal_add_temperature_field.restype = None
        for name in ('heat', 'generation'):
            fn = getattr(lib, 'libfive_tetthermal_add_%s_profile' % name)
            fn.argtypes = [libfive_tetthermal_p, libfive_tree, ctypes.c_float, libfive_tree]
            fn.restype = None
        lib.libfive_tetthermal_add_convection_fields.argtypes = [libfive_tetthermal_p, libfive_tree, ctypes.c_float,
                                                                libfive_tree, ctypes.c_float, libfive_tree]
        lib.libfive_tetthermal_add_convection_fields.restype = None
    lib.libfive_tetthermal_prepare.argtypes = [libfive_tetthermal_p]
    lib.libfive_tetthermal_prepare.restype = ctypes.c_int
    lib.libfive_tetthermal_hash.argtypes = [libfive_tetthermal_p]
    lib.libfive_tetthermal_hash.restype = ctypes.c_uint64
    lib.libfive_tetthermal_save.argtypes = [libfive_tetthermal_p, ctypes.c_char_p]
    lib.libfive_tetthermal_save.restype = ctypes.c_int
    lib.libfive_tetthermal_load.argtypes = [ctypes.c_char_p]
    lib.libfive_tetthermal_load.restype = libfive_tetthermal_p
    lib.libfive_tetthermal_set_salt.argtypes = [libfive_tetthermal_p, ctypes.c_uint64]
    lib.libfive_tetthermal_set_salt.restype = None
    lib.libfive_tetthermal_solve.argtypes = [libfive_tetthermal_p, ctypes.c_int, ctypes.c_float]
    lib.libfive_tetthermal_solve.restype = ctypes.c_int
    lib.libfive_tetthermal_message.argtypes = [libfive_tetthermal_p]
    lib.libfive_tetthermal_message.restype = ctypes.c_char_p
    lib.libfive_tetthermal_field.argtypes = [libfive_tetthermal_p, ctypes.c_int]
    lib.libfive_tetthermal_field.restype = libfive_tree
    lib.libfive_tetthermal_field_min.argtypes = [libfive_tetthermal_p, ctypes.c_int]
    lib.libfive_tetthermal_field_min.restype = ctypes.c_float
    lib.libfive_tetthermal_field_max.argtypes = [libfive_tetthermal_p, ctypes.c_int]
    lib.libfive_tetthermal_field_max.restype = ctypes.c_float
    lib.libfive_tetthermal_stat.argtypes = [libfive_tetthermal_p, ctypes.c_int]
    lib.libfive_tetthermal_stat.restype = ctypes.c_double
    lib.libfive_tetthermal_delete.argtypes = [libfive_tetthermal_p]
    lib.libfive_tetthermal_delete.restype = None
except AttributeError:
    pass    # an older library: no tetrahedral meshing

# Flow on the tetrahedral mesh (stdlib/fluid.py)
libfive_tetflow_p = ctypes.c_void_p
try:
    lib.libfive_tetflow_new.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float, ctypes.c_float, ctypes.c_float]
    lib.libfive_tetflow_new.restype = libfive_tetflow_p
    lib.libfive_tetflow_add_inlet.argtypes = [libfive_tetflow_p, libfive_tree, ctypes.c_float, ctypes.c_float, ctypes.c_float,
                                              ctypes.c_float, ctypes.c_float, ctypes.c_int]
    lib.libfive_tetflow_add_inlet.restype = None
    lib.libfive_tetflow_add_outlet.argtypes = [libfive_tetflow_p, libfive_tree, ctypes.c_float]
    lib.libfive_tetflow_add_outlet.restype = None
    lib.libfive_tetflow_add_wall.argtypes = [libfive_tetflow_p, libfive_tree, ctypes.c_float, ctypes.c_float, ctypes.c_float]
    lib.libfive_tetflow_add_wall.restype = None
    lib.libfive_tetflow_add_slip.argtypes = [libfive_tetflow_p, libfive_tree]
    lib.libfive_tetflow_add_slip.restype = None
    lib.libfive_tetflow_set_gravity.argtypes = [libfive_tetflow_p, ctypes.c_float, ctypes.c_float, ctypes.c_float]
    lib.libfive_tetflow_set_gravity.restype = None
    lib.libfive_tetflow_set_options.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.c_int, ctypes.c_float, ctypes.c_int,
                                                ctypes.c_int, ctypes.c_float, ctypes.c_int]
    lib.libfive_tetflow_set_options.restype = None
    lib.libfive_tetflow_prepare.argtypes = [libfive_tetflow_p]
    lib.libfive_tetflow_prepare.restype = ctypes.c_int
    lib.libfive_tetflow_hash.argtypes = [libfive_tetflow_p]
    lib.libfive_tetflow_hash.restype = ctypes.c_uint64
    lib.libfive_tetflow_save.argtypes = [libfive_tetflow_p, ctypes.c_char_p]
    lib.libfive_tetflow_save.restype = ctypes.c_int
    lib.libfive_tetflow_load.argtypes = [ctypes.c_char_p]
    lib.libfive_tetflow_load.restype = libfive_tetflow_p
    lib.libfive_tetflow_set_salt.argtypes = [libfive_tetflow_p, ctypes.c_uint64]
    lib.libfive_tetflow_set_salt.restype = None
    lib.libfive_tetflow_solve.argtypes = [libfive_tetflow_p, ctypes.c_float]
    lib.libfive_tetflow_solve.restype = ctypes.c_int
    lib.libfive_tetflow_message.argtypes = [libfive_tetflow_p]
    lib.libfive_tetflow_message.restype = ctypes.c_char_p
    lib.libfive_tetflow_warning.argtypes = [libfive_tetflow_p]
    lib.libfive_tetflow_warning.restype = ctypes.c_char_p
    lib.libfive_tetflow_field.argtypes = [libfive_tetflow_p, ctypes.c_int]
    lib.libfive_tetflow_field.restype = libfive_tree
    lib.libfive_tetflow_field_min.argtypes = [libfive_tetflow_p, ctypes.c_int]
    lib.libfive_tetflow_field_min.restype = ctypes.c_float
    lib.libfive_tetflow_field_max.argtypes = [libfive_tetflow_p, ctypes.c_int]
    lib.libfive_tetflow_field_max.restype = ctypes.c_float
    lib.libfive_tetflow_stat.argtypes = [libfive_tetflow_p, ctypes.c_int]
    lib.libfive_tetflow_stat.restype = ctypes.c_double
    lib.libfive_tetflow_items.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double), ctypes.c_int]
    lib.libfive_tetflow_items.restype = ctypes.c_int
    lib.libfive_tetflow_delete.argtypes = [libfive_tetflow_p]
    lib.libfive_tetflow_delete.restype = None
    lib.libfive_tetflow_solve_transient.argtypes = [libfive_tetflow_p, ctypes.c_float, ctypes.c_int, ctypes.c_int, ctypes.c_float]
    lib.libfive_tetflow_solve_transient.restype = ctypes.c_int
    lib.libfive_tetflow_step_count.argtypes = [libfive_tetflow_p]
    lib.libfive_tetflow_step_count.restype = ctypes.c_int
    lib.libfive_tetflow_step_time.argtypes = [libfive_tetflow_p, ctypes.c_int]
    lib.libfive_tetflow_step_time.restype = ctypes.c_double
    lib.libfive_tetflow_step_iteration.argtypes = [libfive_tetflow_p, ctypes.c_int]
    lib.libfive_tetflow_step_iteration.restype = ctypes.c_int
    lib.libfive_tetflow_step_field.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.c_int]
    lib.libfive_tetflow_step_field.restype = libfive_tree
    lib.libfive_tetflow_step_field_min.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.c_int]
    lib.libfive_tetflow_step_field_min.restype = ctypes.c_float
    lib.libfive_tetflow_step_field_max.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.c_int]
    lib.libfive_tetflow_step_field_max.restype = ctypes.c_float
    lib.libfive_tetflow_step_stat.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.c_int]
    lib.libfive_tetflow_step_stat.restype = ctypes.c_double
    lib.libfive_tetflow_streamlines.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double), ctypes.c_int,
                                                ctypes.c_double, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_double),
                                                ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
    lib.libfive_tetflow_streamlines.restype = ctypes.c_int
    lib.libfive_tetflow_inlet_seeds.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
    lib.libfive_tetflow_inlet_seeds.restype = ctypes.c_int
    lib.libfive_tetflow_optimize.argtypes = [libfive_tetflow_p, libfive_tree, libfive_tree, ctypes.c_float, ctypes.c_float,
                                             ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float,
                                             ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_int, ctypes.c_float,
                                             ctypes.POINTER(ctypes.c_void_p), ctypes.c_int, ctypes.POINTER(ctypes.c_void_p), ctypes.c_int,
                                             ctypes.c_int, ctypes.c_float]
    lib.libfive_tetflow_optimize.restype = ctypes.c_int
    lib.libfive_tetflow_direction.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
    lib.libfive_tetflow_direction.restype = None
    lib.libfive_tetflow_level.argtypes = [libfive_tetflow_p]
    lib.libfive_tetflow_level.restype = libfive_tree
    lib.libfive_tetflow_history.argtypes = [libfive_tetflow_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double), ctypes.c_int]
    lib.libfive_tetflow_history.restype = ctypes.c_int
    lib.libfive_tetflow_level_at.argtypes = [libfive_tetflow_p, ctypes.c_int]
    lib.libfive_tetflow_level_at.restype = libfive_tree
except AttributeError:
    pass    # an older library: no flow analysis (or none of its optimisation)

libfive_thermal_p = ctypes.c_void_p
try:
    lib.libfive_thermal_set_element.argtypes = [libfive_thermal_p, ctypes.c_int]
    lib.libfive_thermal_set_element.restype = None
except AttributeError:
    pass    # an older library: hexahedra only
lib.libfive_thermal_new.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float, ctypes.c_float]
lib.libfive_thermal_new.restype = libfive_thermal_p
lib.libfive_thermal_add_temperature.argtypes = [libfive_thermal_p, libfive_tree, ctypes.c_float]
lib.libfive_thermal_add_temperature.restype = None
lib.libfive_thermal_add_heat.argtypes = [libfive_thermal_p, libfive_tree, ctypes.c_float]
lib.libfive_thermal_add_heat.restype = None
lib.libfive_thermal_add_convection.argtypes = [libfive_thermal_p, libfive_tree, ctypes.c_float, ctypes.c_float]
lib.libfive_thermal_add_convection.restype = None
lib.libfive_thermal_solve.argtypes = [libfive_thermal_p, ctypes.c_int, ctypes.c_float]
lib.libfive_thermal_solve.restype = ctypes.c_int
lib.libfive_thermal_message.argtypes = [libfive_thermal_p]
lib.libfive_thermal_message.restype = ctypes.c_char_p
lib.libfive_thermal_field.argtypes = [libfive_thermal_p, ctypes.c_int]
lib.libfive_thermal_field.restype = libfive_tree
lib.libfive_thermal_field_min.argtypes = [libfive_thermal_p, ctypes.c_int]
lib.libfive_thermal_field_min.restype = ctypes.c_float
lib.libfive_thermal_field_max.argtypes = [libfive_thermal_p, ctypes.c_int]
lib.libfive_thermal_field_max.restype = ctypes.c_float
lib.libfive_thermal_stat.argtypes = [libfive_thermal_p, ctypes.c_int]
lib.libfive_thermal_stat.restype = ctypes.c_double
lib.libfive_thermal_delete.argtypes = [libfive_thermal_p]
lib.libfive_thermal_delete.restype = None
lib.libfive_thermal_add_generation.argtypes = [libfive_thermal_p, libfive_tree, ctypes.c_float]
lib.libfive_thermal_add_generation.restype = None
lib.libfive_thermal_prepare.argtypes = [libfive_thermal_p]
lib.libfive_thermal_prepare.restype = ctypes.c_int
lib.libfive_thermal_hash.argtypes = [libfive_thermal_p]
lib.libfive_thermal_hash.restype = ctypes.c_uint64
lib.libfive_thermal_save.argtypes = [libfive_thermal_p, ctypes.c_char_p]
lib.libfive_thermal_save.restype = ctypes.c_int
lib.libfive_thermal_load.argtypes = [ctypes.c_char_p]
lib.libfive_thermal_load.restype = libfive_thermal_p
lib.libfive_thermal_set_salt.argtypes = [libfive_thermal_p, ctypes.c_uint64]
lib.libfive_thermal_set_salt.restype = None
lib.libfive_thermal_optimize.argtypes = [libfive_thermal_p, ctypes.c_float, ctypes.c_float,
                                         ctypes.c_float, ctypes.c_int, ctypes.c_float,
                                         ctypes.POINTER(ctypes.c_void_p), ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_void_p), ctypes.c_int,
                                         ctypes.c_int, ctypes.c_float, ctypes.c_int]
lib.libfive_thermal_optimize.restype = ctypes.c_int
lib.libfive_thermal_density.argtypes = [libfive_thermal_p]
lib.libfive_thermal_density.restype = libfive_tree
lib.libfive_thermal_history.argtypes = [libfive_thermal_p, ctypes.POINTER(ctypes.c_double), ctypes.c_int]
lib.libfive_thermal_history.restype = ctypes.c_int

lib.libfive_tree_eval_d.argtypes = [libfive_tree, libfive_vec3_t]
lib.libfive_tree_eval_d.restype = libfive_vec3_t

lib.libfive_tree_optimized.argtypes = [libfive_tree]
lib.libfive_tree_optimized.restype = libfive_tree

lib.libfive_tree_render_mesh.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float]
lib.libfive_tree_render_mesh.restype = ctypes.POINTER(libfive_mesh_t)
try:
    # the same for a shape with var()s: the trees of the variables and the numbers they stand for (inside the application)
    lib.libfive_tree_render_mesh_vars.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float,
                                                  ctypes.POINTER(libfive_tree), ctypes.POINTER(ctypes.c_float),
                                                  ctypes.c_int]
    lib.libfive_tree_render_mesh_vars.restype = ctypes.POINTER(libfive_mesh_t)
except AttributeError:
    pass    # an older library: the numbers of var()s are read as 0

# algo: 0=dual contouring (default/fastest), 1=iso simplex, 2=hybrid.
# hybrid/simplex are slower but measurably more robust for STEP-imported
# oracle shapes -- dual contouring was found to fragment meshes it
# shouldn't (see step import notes) even when the underlying field is
# provably correct at every queried point.
lib.libfive_tree_render_mesh_algo.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float,
                                               ctypes.c_int, ctypes.c_double, ctypes.c_int]
lib.libfive_tree_render_mesh_algo.restype = ctypes.POINTER(libfive_mesh_t)

lib.libfive_mesh_delete.argtypes = [ctypes.POINTER(libfive_mesh_t)]

# Batch evaluation (newer libraries; optional so older DLLs still load)
try:
    lib.libfive_tree_grid_stats.argtypes = [libfive_tree, ctypes.POINTER(ctypes.c_float),
                                            ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                            ctypes.c_int, ctypes.c_int,
                                            ctypes.POINTER(ctypes.c_double)]
    lib.libfive_tree_grid_stats.restype = ctypes.c_bool
except AttributeError:
    pass

# Graph (beam) lattices (newer libraries; optional)
class libfive_graph_t(ctypes.Structure):
    _fields_ = [("nodes", ctypes.POINTER(ctypes.c_float)),
                ("node_count", ctypes.c_int),
                ("beams", ctypes.POINTER(ctypes.c_int)),
                ("beam_count", ctypes.c_int)]

try:
    lib.libfive_lattice_volume_graph.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float,
                                                 ctypes.c_int, ctypes.c_int, ctypes.c_uint]
    lib.libfive_lattice_volume_graph.restype = ctypes.POINTER(libfive_graph_t)
    lib.libfive_lattice_surface_graph.argtypes = [libfive_tree, libfive_region_t, ctypes.c_float,
                                                  ctypes.c_int, ctypes.c_uint]
    lib.libfive_lattice_surface_graph.restype = ctypes.POINTER(libfive_graph_t)
    lib.libfive_lattice_points_graph.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                                 ctypes.c_int]
    lib.libfive_lattice_points_graph.restype = ctypes.POINTER(libfive_graph_t)
    lib.libfive_graph_delete.argtypes = [ctypes.POINTER(libfive_graph_t)]
    lib.libfive_graph_delete.restype = None
    lib.libfive_lattice_last_error.argtypes = []
    lib.libfive_lattice_last_error.restype = ctypes.c_char_p
    if hasattr(lib, 'libfive_lattice_last_warning'):                # (a library from before the warning existed has none)
        lib.libfive_lattice_last_warning.argtypes = []
        lib.libfive_lattice_last_warning.restype = ctypes.c_char_p
    lib.libfive_beam_lattice.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_int), ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_float), ctypes.c_float]
    lib.libfive_beam_lattice.restype = libfive_tree
except AttributeError:
    pass

try:
    # A strut lattice's cells laid on the surface of a body, from its field alone, as a graph of beams
    lib.libfive_surface_cells.argtypes = [libfive_tree, ctypes.POINTER(libfive_tree), ctypes.POINTER(ctypes.c_float),
                                          ctypes.c_int, ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
                                          ctypes.POINTER(ctypes.c_double), ctypes.c_int,
                                          ctypes.c_double, ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                          ctypes.c_double, ctypes.c_int, ctypes.c_double, ctypes.c_double,
                                          ctypes.POINTER(ctypes.c_double)]
    lib.libfive_surface_cells.restype = ctypes.POINTER(libfive_graph_t)
except AttributeError:
    pass

try:
    # The same cells with a periodic surface (a TPMS) laid on them, as a field
    lib.libfive_surface_tpms.argtypes = [libfive_tree, ctypes.POINTER(libfive_tree), ctypes.POINTER(ctypes.c_float),
                                         ctypes.c_int, ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
                                         ctypes.POINTER(ctypes.c_double), ctypes.c_int,
                                         ctypes.c_double, ctypes.c_double, ctypes.c_int, ctypes.c_double, ctypes.c_int,
                                         ctypes.c_double, ctypes.c_int, ctypes.c_double, ctypes.c_double,
                                         ctypes.POINTER(ctypes.c_double)]
    lib.libfive_surface_tpms.restype = libfive_tree
except AttributeError:
    pass

# Analysis fields (newer libraries; optional)
try:
    lib.libfive_field_gradient.argtypes = [libfive_tree, ctypes.c_int]
    lib.libfive_field_gradient.restype = libfive_tree
    lib.libfive_field_thickness.argtypes = [libfive_tree, ctypes.c_float]
    lib.libfive_field_thickness.restype = libfive_tree
    lib.libfive_field_curvature.argtypes = [libfive_tree, ctypes.c_float]
    lib.libfive_field_curvature.restype = libfive_tree
except AttributeError:
    pass

# Topology optimization (newer libraries; optional)
try:
    lib.libfive_fea_optimize.argtypes = [libfive_fea_p, ctypes.c_float, ctypes.c_float,
                                         ctypes.c_float, ctypes.c_int, ctypes.c_float,
                                         ctypes.POINTER(ctypes.c_void_p), ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_void_p), ctypes.c_int,
                                         ctypes.c_int, ctypes.c_float, ctypes.c_int]
    lib.libfive_fea_optimize.restype = ctypes.c_int
    lib.libfive_fea_density.argtypes = [libfive_fea_p]
    lib.libfive_fea_density.restype = libfive_tree
    lib.libfive_fea_history.argtypes = [libfive_fea_p, ctypes.POINTER(ctypes.c_double), ctypes.c_int]
    lib.libfive_fea_history.restype = ctypes.c_int
except AttributeError:
    pass

# Data fields (newer libraries; optional)
try:
    lib.libfive_field_points.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
                                         ctypes.c_int, ctypes.c_int, ctypes.c_float]
    lib.libfive_field_points.restype = libfive_tree
    lib.libfive_field_noise.argtypes = [ctypes.c_float, ctypes.c_int, ctypes.c_uint, ctypes.c_float,
                                        ctypes.c_float]
    lib.libfive_field_noise.restype = libfive_tree
except AttributeError:
    pass
