"""Build isolated G20A BSP/plane/light prototypes using the shared toolchain.

No original executable analysis; derived tables come from the existing G03
package. Baseline objects are retained and linked in the original order.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

from research_gba_fps import command
from experiment_cleanup import clean_objects_after_build
from gba_hw_bg_assets import hardware_background_tables

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = {
    'baseline': (),
    'no_scales': ('NO_SCALES',),
    'no_scales_runtime': ('NO_SCALES_RUNTIME',),
    'planes': ('FAST_PLANES',),
    'light': ('NO_SCALES', 'PRELIT'),
    'bsp': ('BSP',),
    'combined': ('NO_SCALES', 'FAST_PLANES', 'PRELIT', 'BSP'),
    'world_planes': ('NO_SCALES', 'WORLD_PLANES'),
    'world_combined': ('NO_SCALES', 'WORLD_PLANES', 'PRELIT', 'BSP'),
    'sweep': ('NO_SCALES', 'FAST_PLANES', 'SWEEP_PLANES', 'PRELIT'),
    'low_vertical': ('NO_SCALES', 'FAST_PLANES', 'SWEEP_PLANES', 'PRELIT', 'LOW_VERTICAL'),
    'low_bsp': ('NO_SCALES', 'FAST_PLANES', 'SWEEP_PLANES', 'PRELIT', 'LOW_VERTICAL', 'BSP'),
    'procedural': ('NO_SCALES', 'WORLD_PLANES', 'PRELIT', 'PROCEDURAL'),
    'setup': ('STATIC_SETUP',),
    'textured': ('NO_SCALES','STATIC_SETUP','FAST_PLANES','SWEEP_PLANES','PRELIT'),
    'bsp_setup': ('STATIC_SETUP','BSP'),
    'exact': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT'),
    'exact_planes': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES'),
    'thumb_exact': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','THUMB_RENDERER'),
    'thumb_ram': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','THUMB_RENDERER','RAM_GEOMETRY'),
    'world_setup': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT'),
    'world_spans': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS'),
    'world_merged': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','MERGE_CEILINGS'),
    'exact_ui': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE'),
    'world_ui': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE'),
    'world_merged_ui': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','MERGE_CEILINGS','UI_CACHE'),
    'ray_world': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','RAY_WALLS'),
    'world_water': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS'),
    'world_coarse': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','COARSE_PLANES'),
    'backface_exact': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','BACKFACE'),
    'backface_world': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','BACKFACE'),
    'backface_coarse': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','COARSE_PLANES','BACKFACE'),
    'world_div': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','WORLD_DIV','EXACT_PLANES'),
    'coarse_div': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','COARSE_PLANES','WORLD_DIV','EXACT_PLANES'),
    'sides_exact': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT'),
    'sides_world': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','SIDES_PRELIT'),
    'sides_coarse': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','COARSE_PLANES','SIDES_PRELIT'),
    'raster_exact': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','RASTER_DIV'),
    'raster_world': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','SIDES_PRELIT','RASTER_DIV','EXACT_PLANES'),
    'raster_coarse': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','COARSE_PLANES','SIDES_PRELIT','RASTER_DIV','EXACT_PLANES'),
    'eager_exact': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','EAGER_CLEAR'),
    'eager_coarse': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','COARSE_PLANES','SIDES_PRELIT','EAGER_CLEAR'),
    'masked_exact': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','MASKED_ARM'),
    'masked_world': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','SIDES_PRELIT','MASKED_ARM'),
    'masked_coarse': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','COARSE_PLANES','SIDES_PRELIT','MASKED_ARM'),
    'lookup_world': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','SIDES_PRELIT','WORLD_LOOKUP'),
    'ray_sides': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','SIDES_PRELIT','RAY_WALLS'),
    'ray_coarse': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','COARSE_PLANES','SIDES_PRELIT','RAY_WALLS'),
    'vertex_exact': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','GEOMETRY_CACHE'),
    'vertex_world': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','SIDES_PRELIT','GEOMETRY_CACHE'),
    'vertex_coarse': ('NO_SCALES','STATIC_SETUP','WORLD_PLANES','PRELIT','EXACT_LIGHT','WORLD_SPANS','UI_CACHE','WATER_SPANS','COARSE_PLANES','SIDES_PRELIT','GEOMETRY_CACHE'),
    'hw_obj': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ'),
    'hw_obj_arm': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM'),
    'main_control': ('MAIN_CONTROL',),
    'hw_direct': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT'),
    'source_control': ('MAIN_CONTROL','UI_CONTROL'),
    'hw_background': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG'),
    'hw_rays32': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32'),
    'hw_lease': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE'),
    'hw_rowlut': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT'),
    'hw_stream': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM'),
    'hw_ceiling2': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_CEILING2'),
    'hw_div': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','RASTER_DIV','HW_EDGE_ROM'),
    'hw_hud_cache': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE'),
    'hw_profile': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','RASTER_PROFILE'),
    'hw_backface': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM'),
    'hw_backface_sides': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES'),
    'hw_side_setup': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES','SIDE_SETUP'),
    'hw_opaque_gaps': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES','SIDE_SETUP','FAST_OPAQUE_ARM','OPAQUE_GAPS'),
    'hw_masked': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES','SIDE_SETUP','HW_MASKED'),
    'hw_present': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES','SIDE_SETUP','HW_MASKED','HW_PRESENT'),
    'hw_present_map': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES','SIDE_SETUP','HW_MASKED','HW_PRESENT','HW_MAP_CACHE'),
    'hw_early_present': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES','SIDE_SETUP','HW_MASKED','HW_PRESENT','HW_MAP_CACHE','HW_EARLY_PRESENT'),
    'hw_vertical128': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES','SIDE_SETUP','HW_MASKED','HW_PRESENT','HW_MAP_CACHE','HW_EARLY_PRESENT','HW_VERTICAL128'),
    'hw_present_plain': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES','SIDE_SETUP','HW_PRESENT','HW_MAP_CACHE'),
    'hw_gaps_masked': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','HW_BG_STREAM','HW_UI_REUSE','BACKFACE','HW_EDGE_ROM','HW_COVER_ROM','BACKFACE_SIDES','SIDE_SETUP','FAST_OPAQUE_ARM','OPAQUE_GAPS','HW_MASKED'),
    'hw_opaque': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','FAST_OPAQUE'),
    'hw_opaque_arm': ('NO_SCALES','STATIC_SETUP','PRELIT','EXACT_LIGHT','EXACT_PLANES','UI_CACHE','SIDES_PRELIT','HW_OBJ','HW_PACK_ARM','HW_DIRECT','HW_BG','HW_RAYS32','HW_BG_LEASE_CACHE','HW_ROW_LUT','FAST_OPAQUE_ARM'),
}

VARIANTS['hw_rows128']=VARIANTS['hw_vertical128']+('HW_OBJ_ROWS_PROBE',)
VARIANTS['hw_rows144']=VARIANTS['hw_early_present']+('HW_OBJ_ROWS_PROBE',)
VARIANTS['hw_merge_rims']=VARIANTS['hw_early_present']+('HW_MERGE_RIMS',)
VARIANTS['hw_v128_rims']=VARIANTS['hw_vertical128']+('HW_MERGE_RIMS',)
VARIANTS['hw_column_cache']=VARIANTS['hw_early_present']+('HW_COLUMN_CACHE',)
VARIANTS['hw_v128_columns']=VARIANTS['hw_vertical128']+('HW_COLUMN_CACHE',)
VARIANTS['hw_affine_columns']=VARIANTS['hw_column_cache']+('HW_AFFINE_U','HW_INTERVAL_EWRAM')
VARIANTS['hw_v128_affine']=VARIANTS['hw_v128_columns']+('HW_AFFINE_U','HW_INTERVAL_EWRAM')
VARIANTS['hw_u_recip']=VARIANTS['hw_column_cache']+('HW_U_RECIP',)
VARIANTS['hw_v128_recip']=VARIANTS['hw_v128_columns']+('HW_U_RECIP',)
VARIANTS['hw_anim_cache']=VARIANTS['hw_column_cache']+('HW_ANIM_CACHE',)
VARIANTS['hw_v128_anim']=VARIANTS['hw_v128_columns']+('HW_ANIM_CACHE',)
VARIANTS['hw_geometry_profile']=VARIANTS['hw_column_cache']+('HW_GEOMETRY_PROFILE',)
VARIANTS['hw_cell_class']=VARIANTS['hw_column_cache']+('HW_CELL_CLASS',)
VARIANTS['hw_v128_class']=VARIANTS['hw_v128_columns']+('HW_CELL_CLASS',)
VARIANTS['hw_cell_seed']=VARIANTS['hw_cell_class']+('HW_FC_SEED',)
VARIANTS['hw_v128_seed']=VARIANTS['hw_v128_class']+('HW_FC_SEED',)
VARIANTS['hw_cell_near']=VARIANTS['hw_cell_seed']+('HW_NEAR_FC',)
VARIANTS['hw_v128_near']=VARIANTS['hw_v128_seed']+('HW_NEAR_FC',)
VARIANTS['hw_ceil_prelit']=VARIANTS['hw_cell_seed']+('HW_SURFACE_PRELIT',)
VARIANTS['hw_v128_ceil']=VARIANTS['hw_v128_seed']+('HW_SURFACE_PRELIT',)
VARIANTS['hw_face_box']=VARIANTS['hw_cell_class']+('HW_FACE_BOX',)
VARIANTS['hw_v128_box']=VARIANTS['hw_v128_seed']+('HW_FACE_BOX',)
VARIANTS['hw_v128_affine_div']=VARIANTS['hw_v128_box']+('HW_AFFINE_DIV',)
VARIANTS['hw_v128_face_light']=VARIANTS['hw_v128_box']+('HW_FACE_LIGHT',)
VARIANTS['hw_v128_raster_profile']=VARIANTS['hw_v128_box']+('RASTER_PROFILE',)
VARIANTS['hw_v128_cover4']=VARIANTS['hw_v128_box']+('HW_COVER4',)
VARIANTS['hw_v128_hot_cover']=tuple(d for d in VARIANTS['hw_v128_cover4'] if d!='HW_COVER_ROM')
VARIANTS['hw_v128_hot_edges']=tuple(d for d in VARIANTS['hw_v128_cover4'] if d not in ('HW_COVER_ROM','HW_EDGE_ROM'))
VARIANTS['hw_v128_direct_run']=VARIANTS['hw_v128_hot_cover']+('HW_DIRECT_RUN',)
VARIANTS['hw_v128_direct_edges']=VARIANTS['hw_v128_hot_edges']+('HW_DIRECT_RUN',)
VARIANTS['hw_v128_projection']=VARIANTS['hw_v128_direct_run']+('HW_PROJECT_RECIP',)
VARIANTS['hw_v128_vstep']=VARIANTS['hw_v128_projection']+('HW_VSTEP_DIV',)
VARIANTS['hw_v128_arith']=VARIANTS['hw_v128_projection']+('HW_ARITH_RECIP',)
VARIANTS['hw_v128_nonzero']=VARIANTS['hw_v128_projection']+('HW_NONZERO_COLUMN',)
VARIANTS['hw_v128_compact']=VARIANTS['hw_v128_nonzero']+('HW_COMPACT_ZERO',)
VARIANTS['hw_v128_light4']=VARIANTS['hw_v128_compact']+('HW_LIGHT4',)
VARIANTS['hw_v128_bsp_reuse']=VARIANTS['hw_v128_compact']+('BSP','HW_BSP_REUSE')
VARIANTS['hw_v128_bsp_block2']=VARIANTS['hw_v128_bsp_reuse']+('HW_BSP_BLOCK2',)
VARIANTS['hw_v128_bsp_block4']=VARIANTS['hw_v128_bsp_reuse']+('HW_BSP_BLOCK4',)
VARIANTS['hw_v128_bsp_thumb']=VARIANTS['hw_v128_bsp_block4']+('HW_BSP_THUMB',)
VARIANTS['hw_v128_surface_split']=VARIANTS['hw_v128_compact']+('HW_SURFACE_SPLIT',)
VARIANTS['hw_v128_linear_rom']=VARIANTS['hw_v128_surface_split']+('HW_LINEAR_ROM',)
VARIANTS['hw_v128_hot_sides']=VARIANTS['hw_v128_linear_rom']+('HW_HOT_SIDES','HW_EDGE_RAM_CALL')
VARIANTS['hw_v128_hot_intervals']=VARIANTS['hw_v128_linear_rom']+('HW_HOT_COVER','HW_EDGE_RAM_CALL')
VARIANTS['hw_v128_hot_helpers']=VARIANTS['hw_v128_hot_intervals']+('HW_HOT_IMAGE','HW_HOT_BILLBOARD')
VARIANTS['hw_v128_rom_cache']=VARIANTS['hw_v128_linear_rom']+('HW_ROM_COLUMN_CACHE',)
VARIANTS['hw_v128_cache_intervals']=VARIANTS['hw_v128_hot_intervals']+('HW_ROM_COLUMN_CACHE',)
VARIANTS['hw_v128_span_solid']=VARIANTS['hw_v128_linear_rom']+('HW_SPAN_SOLID',)
VARIANTS['hw_v128_span_hot']=VARIANTS['hw_v128_hot_intervals']+('HW_SPAN_SOLID','HW_HOT_IMAGE')
VARIANTS['hw_v128_direct_gaps']=VARIANTS['hw_v128_linear_rom']+('HW_DIRECT_GAPS',)
VARIANTS['hw_v128_static_geometry']=VARIANTS['hw_v128_direct_gaps']+('HW_STATIC_GEOMETRY',)
VARIANTS['hw_v128_static_hot']=VARIANTS['hw_v128_static_geometry']+('HW_HOT_IMAGE','HW_EDGE_RAM_CALL','HW_INTERVAL_EWRAM')
VARIANTS['hw_v128_entity_prelit']=VARIANTS['hw_v128_static_hot']+('HW_ENTITY_PRELIT',)
VARIANTS['hw_v128_entity_batch']=VARIANTS['hw_v128_entity_prelit']+('HW_MASKED_BATCH',)
VARIANTS['hw_v128_entity_affine']=tuple(d for d in VARIANTS['hw_v128_entity_batch'] if d!='HW_HOT_IMAGE')+('HW_AFFINE_U',)
VARIANTS['hw_v128_entity_wide']=tuple(d for d in VARIANTS['hw_v128_entity_batch'] if d!='HW_HOT_IMAGE')+('HW_U_WIDE',)
VARIANTS['hw_v128_entity_rect']=tuple(d for d in VARIANTS['hw_v128_entity_batch'] if d!='HW_HOT_IMAGE')+('HW_RECT_RASTER',)
VARIANTS['hw_v128_sparse_control']=tuple(d for d in VARIANTS['hw_v128_entity_rect'] if d!='HW_EDGE_RAM_CALL')
VARIANTS['hw_v128_sparse_posts']=VARIANTS['hw_v128_sparse_control']+('HW_SPARSE_POSTS',)
VARIANTS['hw_v128_world_obj_probe']=VARIANTS['hw_v128_entity_rect']+('HW_WORLD_OBJ_PROBE',)
VARIANTS['hw_v128_world_obj']=tuple(d for d in VARIANTS['hw_v128_entity_rect'] if d!='HW_EDGE_RAM_CALL')+('HW_VISIBLE_SPLIT','HW_WORLD_OBJ')
VARIANTS['hw_v128_surface_clip']=tuple(d for d in VARIANTS['hw_v128_entity_rect'] if d!='HW_EDGE_RAM_CALL')+('HW_SURFACE_CLIP',)
VARIANTS['hw_v128_ui_objects']=VARIANTS['hw_v128_entity_rect']+('HW_UI_OBJ',)
VARIANTS['hw_v128_lowered']=VARIANTS['hw_v128_ui_objects']+('HW_BG_LOWERED',)
VARIANTS['hw_v128_lowered_ram']=VARIANTS['hw_v128_lowered']+('HW_OAM_RAM',)
VARIANTS['hw_v128_low_adaptive']=tuple(d for d in VARIANTS['hw_v128_lowered'] if d!='HW_EDGE_RAM_CALL')+('HW_LOW_ADAPTIVE',)
VARIANTS['hw_v128_low_grid4']=VARIANTS['hw_v128_low_adaptive']+('HW_LOW_GRID4',)
VARIANTS['hw_rows96']=tuple(d for d in VARIANTS['hw_v128_entity_rect'] if d!='HW_EDGE_RAM_CALL')+('HW_ROWS96',)
VARIANTS['hw_rows72']=tuple(d for d in VARIANTS['hw_v128_entity_rect'] if d!='HW_EDGE_RAM_CALL')+('HW_ROWS72',)
VARIANTS['hw_rows96_probe']=VARIANTS['hw_rows96']+('HW_OBJ_ROWS_PROBE',)
VARIANTS['hw_rows72_probe']=VARIANTS['hw_rows72']+('HW_OBJ_ROWS_PROBE',)
VARIANTS['hw_rows96_opaque_batch']=VARIANTS['hw_rows96']+('HW_OPAQUE_BATCH',)
VARIANTS['hw_rows96_class_profile']=VARIANTS['hw_rows96']+('HW_FACE_CLASS_PROFILE',)
VARIANTS['hw_rows96_visible_split']=VARIANTS['hw_rows96']+('HW_VISIBLE_SPLIT',)
VARIANTS['hw_rows96_batch_split']=VARIANTS['hw_rows96_opaque_batch']+('HW_VISIBLE_SPLIT',)
VARIANTS['hw_rows96_alpha_empty']=VARIANTS['hw_rows96_visible_split']+('HW_ALPHA_EMPTY',)
VARIANTS['hw_rows96_batch_alpha']=VARIANTS['hw_rows96_batch_split']+('HW_ALPHA_EMPTY',)
VARIANTS['hw_v128_low_feedback']=VARIANTS['hw_v128_low_adaptive']+('HW_LOW_FEEDBACK','HW_VISIBLE_SPLIT','HW_OPAQUE_BATCH')
VARIANTS['hw_rows96_low_feedback']=VARIANTS['hw_v128_low_feedback']+('HW_ROWS96',)
VARIANTS['hw_v128_stream_bsp']=tuple(d for d in VARIANTS['hw_v128_entity_rect'] if d not in ('HW_STATIC_GEOMETRY','HW_EDGE_RAM_CALL'))+('HW_VISIBLE_SPLIT','STREAM_BSP')
VARIANTS['hw_v128_columns']=tuple(d for d in VARIANTS['hw_v128_entity_rect'] if d not in ('HW_STATIC_GEOMETRY','HW_EDGE_RAM_CALL','HW_SURFACE_SPLIT','HW_RECT_RASTER'))+('HW_VISIBLE_SPLIT','COLUMN_PIPELINE')
VARIANTS['hw_v128_sector_clip']=tuple(d for d in VARIANTS['hw_v128_entity_rect'] if d not in ('HW_STATIC_GEOMETRY','HW_EDGE_RAM_CALL','HW_SURFACE_SPLIT'))+('HW_VISIBLE_SPLIT','SECTOR_CLIP')
VARIANTS['hw_v128_sector_bitmap']=tuple(d for d in VARIANTS['hw_v128_sector_clip'] if d not in ('HW_BG_STREAM','HW_PRESENT','HW_EARLY_PRESENT','HW_OAM_RAM'))+('HW_UI_OBJ','SECTOR_BITMAP')
VARIANTS['hw_v128_sector_clip_hot']=VARIANTS['hw_v128_sector_clip']+('SECTOR_HOT',)
VARIANTS['hw_v128_sector_bitmap_hot']=VARIANTS['hw_v128_sector_bitmap']+('SECTOR_HOT',)
VARIANTS['hw_v128_sector_gpu']=tuple(d for d in VARIANTS['hw_v128_sector_clip_hot'] if d not in ('HW_PRESENT','HW_EARLY_PRESENT','HW_OAM_RAM'))+('HW_UI_OBJ','SECTOR_GPU')
VARIANTS['hw_v128_sector_gpu_off']=VARIANTS['hw_v128_sector_gpu']+('SECTOR_GPU_OFF',)
VARIANTS['hw_v128_sector_gpu_thumb']=VARIANTS['hw_v128_sector_gpu']+('SECTOR_GPU_THUMB',)
VARIANTS['hw_v128_sector_gpu_shear']=VARIANTS['hw_v128_sector_gpu_thumb']+('SECTOR_GPU_SHEAR',)
VARIANTS['hw_v128_bsp_hot']=VARIANTS['hw_v128_bsp_block4']+('HW_SURFACE_SPLIT','HW_BSP_LEAF_RAM','HW_LINEAR_ROM')
VARIANTS['hw_v128_bsp_hot_thumb']=VARIANTS['hw_v128_bsp_thumb']+('HW_SURFACE_SPLIT','HW_BSP_LEAF_RAM','HW_LINEAR_ROM')
VARIANTS['hw_v128_u_track']=VARIANTS['hw_v128_direct_run']+('HW_U_TRACK','HW_CONTAINS_CALL')
VARIANTS['hw_v128_track_project']=VARIANTS['hw_v128_projection']+('HW_U_TRACK','HW_CONTAINS_CALL','HW_INTERVAL_EWRAM')
VARIANTS['hw_face_light']=VARIANTS['hw_cell_seed']+('HW_FACE_LIGHT',)
VARIANTS['hw_v128_contains']=VARIANTS['hw_v128_box']+('HW_CONTAINS_CALL',)
VARIANTS['hw_v128_combo']=VARIANTS['hw_v128_box']+('HW_CONTAINS_CALL','HW_U_RECIP','HW_SURFACE_PRELIT')
VARIANTS['hw_v128_combo_near']=VARIANTS['hw_v128_combo']+('HW_NEAR_FC',)
VARIANTS['hw_v128_wide']=tuple(d for d in VARIANTS['hw_v128_combo_near'] if d!='HW_U_RECIP')+('HW_U_WIDE',)


def tables(out, assets, include_sides=False, include_zero_spans=False, include_entities=False, include_lowered=False, include_empty=False, scene_rows=128):
    assert not(include_zero_spans and include_empty),'Separate span-bitmap and empty-column asset experiments'
    data = assets.read_bytes()
    u16 = lambda at: struct.unpack_from('<H', data, at)[0]
    u32 = lambda at: struct.unpack_from('<I', data, at)[0]
    directory, views, count = u32(24), u32(28), u32(20)
    bounds, padding = [], []
    bank = bytearray(4)
    offsets = [0] * (6 * (count + 1) * 5)
    exact_offsets=[0]*(6*(count+1)*16)
    procedural = bytearray(6*256*2*3)
    insets=[];static_cells=bytearray();side_resources=[];sprite_classes=bytearray()
    ceiling_bank=bytearray(4);ceiling_offsets=[];ceiling_cache={}
    panel_bank=bytearray();panel_cache={};panel_offsets=[];panel_sources=[]
    hw_panel_bank=bytearray();hw_panel_cache={};hw_panel_offsets=[]
    panel_rid=next(i+1 for i in range(count) if data[directory+i*32:directory+i*32+16].split(b'\0',1)[0]=='ПАНЕЛЬ'.encode('cp866'))
    for language in (0,1):
        at=directory+(panel_rid-1)*32+16+language*8;start,size=u32(at),u32(at+4)
        payload=data[start:start+size];sw,sh=struct.unpack_from('<HH',payload)
        assert (sw,sh)==(320,39) and size==18+sw*sh
        panel_sources.append(start+18)
        for width,height in [(240,16),(16,16),*((w,14) for w in range(24,69,4))]:
            pixels=bytes(payload[18+(r if r<3 else sh-(height-r) if r+3>=height else 5)*sw+
                                 (c if c<3 else sw-(width-c) if c+3>=width else 10)]
                         for r in range(height) for c in range(width))
            if pixels not in panel_cache:panel_cache[pixels]=len(panel_bank);panel_bank.extend(pixels)
            panel_offsets.append(panel_cache[pixels])
            # Transparent padding is outside the exact, nonzero HUD panel.
            assert 0 not in pixels
            cols=(width+7)//8
            tiled=bytes(pixels[y*width+x] if x<width and y<height else 0
                        for ty in range(2) for tx in range(cols) for py in range(8) for px in range(8)
                        for x,y in [(tx*8+px,ty*8+py)])
            if tiled not in hw_panel_cache:hw_panel_cache[tiled]=len(hw_panel_bank);hw_panel_bank.extend(tiled)
            hw_panel_offsets.append(hw_panel_cache[tiled])
    columns = bytearray(6 * (count + 1) * 8)
    cache, column_cache = {}, {}
    for view in range(6):
        rec = views + view * 80
        defs, fields, maps, glyphs = u32(rec+36), u32(rec+40), u32(rec+48), u32(rec+32)
        links = {(u16(u32(rec+72)+i*8), u16(u32(rec+72)+i*8+2)):
                 u16(u32(rec+72)+i*8+4) for i in range(u16(rec+76))}

        def resource(rid):
            entry = directory + (rid-1)*32 + 16 + (view % 2)*8
            return data[u32(entry):u32(entry)+u32(entry+4)]

        light = resource(links[0xffff, 0xfff4])
        for glyph in range(256):
            token=u16(glyphs+glyph*2)
            cls=u16(defs+(token-1)*32) if token and token<=u16(rec+16) else None
            sprite_classes.append(4 if cls is None else int(cls in (3,6,9,10))|(int(cls in (3,9))<<1))
        assert len(light) == 4096 and light[:256] == bytes(range(256))
        # A transparent source texel must remain transparent in every band.
        assert all(light[row*256] == 0 for row in range(16))
        palette=resource(links[0xffff,0xfff3])
        fc_ids={u16(glyphs+2*g)-1 for g in data[maps+8192:maps+12288]}
        ceiling_ids={links[obj,0x56] for obj in fc_ids};assert len(ceiling_ids)==1
        ceiling=resource(next(iter(ceiling_ids)));cw,ch=struct.unpack_from('<HH',ceiling)
        assert len(ceiling)==18+cw*ch
        for row in range(16):
            colors=bytes(light[row*256+t] for t in ceiling[18:])
            if colors not in ceiling_cache:ceiling_cache[colors]=len(ceiling_bank);ceiling_bank.extend(colors)
            ceiling_offsets.append(ceiling_cache[colors])
        for obj in fc_ids:
            d=defs+obj*32;glyph=data[d+10]
            for plane in (0,1):
                slot=(0xfff0 if u16(d) in (12,13) else 0x5a) if not plane else 0x56
                image=resource(links[obj,slot]);hist=Counter(image[18:])
                ranked=sorted(hist,key=lambda t:sum(palette[t*3:t*3+3]))
                total=sum(hist.values());colors=[]
                for percentile in (0.4,0.6,0.12):
                    n=0
                    for color in ranked:
                        n+=hist[color]
                        if n>=total*percentile:break
                    colors.append(color)
                at=(view*256+glyph)*6+plane*3
                procedural[at:at+3]=bytes(colors)
        inset_table=[0]*256
        for obj in fc_ids:
            d=defs+obj*32
            for j in range(u16(d+14),u16(d+14)+u16(d+16)):
                if u16(fields+j*8)==0x54:inset_table[obj]=struct.unpack_from('<i',data,fields+j*8+4)[0]
        insets.extend(inset_table)
        side_ids=[links.get((obj,0x5e),0) if obj in fc_ids and inset_table[obj]>0 else 0 for obj in range(256)]
        side_resources.extend(side_ids)
        classes=bytes(u16(defs+(u16(glyphs+g*2)-1)*32) for g in data[maps+4096:maps+8192])
        floors=bytes(u16(glyphs+g*2)-1 for g in data[maps+8192:maps+12288])
        sides=bytearray(4096)
        for z in range(64):
            for x in range(64):
                at=z*64+x;value=inset_table[floors[at]]
                if value>0:
                    for k,(dx,dz) in enumerate(((0,-1),(-1,0),(0,1),(1,0))):
                        if inset_table[floors[max(0,min(63,z+dz))*64+max(0,min(63,x+dx))]]<value:sides[at]|=1<<k
        static_cells.extend(classes+floors+sides)
        geometries, pad = [], 0
        for cell in range(4096):
            obj = u16(glyphs+2*data[maps+4096+cell])-1
            floor = u16(glyphs+2*data[maps+8192+cell])-1
            assert min(obj, floor) >= 0
            d, fc = defs+obj*32, defs+floor*32
            height = struct.unpack_from('<h', data, fc+8)[0]
            inset = None
            for j in range(u16(fc+14),u16(fc+14)+u16(fc+16)):
                if u16(fields+j*8) == 0x54:
                    inset = struct.unpack_from('<i', data, fields+j*8+4)[0]
            assert inset is not None
            low, high = min(-height, -960, inset-961), max(-height, -960, inset-961)
            if u16(d) in (2,4,7):
                image = resource(links[obj,0xfff0])
                w,h = struct.unpack_from('<HH', image)
                scale = struct.unpack_from('<h', data, d+6)[0]
                assert 0 < w*scale <= 4096 and 0 < h*scale <= 4096
                # Include any legal inset-dependent wall crop and ceiling side.
                low = min(low, -height-h*scale-1)
                high = max(high, -height)
                pad = max(pad, w*scale-480)
            geometries.append((low,high))
        nodes = []

        def tree(x,z,sx,sz):
            index = len(nodes)
            nodes.append(None)
            if sx == sz == 1:
                value = geometries[z*64+x]
            elif sx >= sz:
                a=tree(x,z,sx//2,sz); b=tree(x+sx//2,z,sx//2,sz)
                value=(min(a[0],b[0]),max(a[1],b[1]))
            else:
                a=tree(x,z,sx,sz//2); b=tree(x,z+sz//2,sx,sz//2)
                value=(min(a[0],b[0]),max(a[1],b[1]))
            nodes[index] = value
            return value
        tree(0,0,64,64)
        assert len(nodes) == 8191
        bounds.extend(nodes); padding.append(pad)
        column_resources=[]
        for obj in range(u16(rec+16)):
            d = defs+obj*32
            if u16(d) not in ((2,3,4,6,7,9,10) if include_entities else (2,4,7)):
                continue
            rid=links.get((obj,0xfff0),0)
            if rid:column_resources.append(rid)
        if include_sides:column_resources.extend(sorted(set(side_ids)-{0}))
        for rid in sorted(set(column_resources)) if include_entities else column_resources:
            im = resource(rid)
            w,h = struct.unpack_from('<HH', im)
            assert len(im) == 18+w*h
            raw = bytes(im[18+y*w+x] for x in range(w) for y in range(h))
            column_key=(w,h,raw) if include_entities else raw
            if column_key not in column_cache:
                columns.extend(bytes(-len(columns)%4))
                columns.extend(bytes(int(0 not in raw[x*h:(x+1)*h]) for x in range(w)))
                columns.extend(bytes(256-w))
                column_cache[column_key] = len(columns)
                columns.extend(raw)
            struct.pack_into('<II', columns, (view*(count+1)+rid)*8, column_cache[column_key], 0)
            for row in range(16):
                colors = bytes(light[row*256+t] for t in raw)
                cache_key=(w,h,colors) if include_zero_spans or include_entities else colors
                if cache_key not in cache:
                    bank.extend(bytes(-len(bank)%4))
                    if include_zero_spans:
                        for x in range(w):
                            column=colors[x*h:(x+1)*h]
                            mask=sum(1<<(y//16) for y in range(0,h,16) if 0 in column[y:y+16])
                            bank.extend(struct.pack('<I',mask))
                    if include_empty:
                        bank.extend(bytes(int(not any(colors[x*h:(x+1)*h])) for x in range(w)))
                        bank.extend(bytes(256-w))
                    bank.extend(bytes(int(0 not in colors[x*h:(x+1)*h]) for x in range(w)))
                    bank.extend(bytes(256-w))
                    cache[cache_key] = len(bank); bank.extend(colors)
                exact_offsets[(view*(count+1)+rid)*16+row]=cache[cache_key]
                if row in (0,4,8,12,15):
                    band=(0,4,8,12,15).index(row)
                    offsets[(view*(count+1)+rid)*5+band] = cache[cache_key]
    (out/'bounds.bin').write_bytes(b''.join(struct.pack('<hh',*b) for b in bounds))
    (out/'prelit.bin').write_bytes(bank)
    (out/'columns.bin').write_bytes(columns)
    (out/'procedural.bin').write_bytes(procedural)
    (out/'insets.bin').write_bytes(struct.pack('<'+'h'*len(insets),*insets))
    (out/'static-cells.bin').write_bytes(static_cells)
    (out/'side-resources.bin').write_bytes(struct.pack('<1536H',*side_resources))
    (out/'sprite-classes.bin').write_bytes(sprite_classes)
    (out/'ceiling-bank.bin').write_bytes(ceiling_bank)
    (out/'ceiling-offsets.bin').write_bytes(struct.pack('<96I',*ceiling_offsets))
    (out/'panels.bin').write_bytes(panel_bank)
    (out/'panel-offsets.bin').write_bytes(struct.pack('<28I',*panel_offsets))
    (out/'hw-panels.bin').write_bytes(hw_panel_bank)
    (out/'hw-panel-offsets.bin').write_bytes(struct.pack('<28I',*hw_panel_offsets))
    (out/'offsets.bin').write_bytes(struct.pack('<'+'I'*len(offsets),*offsets))
    (out/'exact-offsets.bin').write_bytes(struct.pack('<'+'I'*len(exact_offsets),*exact_offsets))
    (out/'reciprocal.bin').write_bytes(struct.pack('<16385I',0,*((1<<31)//n for n in range(1,16385))))
    (out/'exact-reciprocal.bin').write_bytes(struct.pack('<16385I',0,0xffffffff,*((1<<32)//n for n in range(2,16385))))
    hardware_bg=hardware_background_tables(out,data,scene_rows)
    if include_lowered:
        from gba_hw_bg_assets import hardware_lowered_tables
        hardware_bg['lowered']=hardware_lowered_tables(out,data,hardware_bg)
    assembly = []
    for symbol,filename in (('dok_bsp_bounds','bounds.bin'),('dok_prelit_offsets','offsets.bin'),
                            ('dok_prelit_bank','prelit.bin'),('dok_plane_reciprocal','reciprocal.bin'),
                            ('dok_procedural_colors','procedural.bin'),
                            ('dok_static_insets','insets.bin'),('dok_static_cells','static-cells.bin'),
                            ('dok_side_resources','side-resources.bin'),
                            ('dok_sprite_class_masks','sprite-classes.bin'),
                            ('dok_ceiling_bank','ceiling-bank.bin'),('dok_ceiling_offsets','ceiling-offsets.bin'),
                            ('dok_exact_prelit_offsets','exact-offsets.bin'),
                            ('dok_exact_plane_reciprocal','exact-reciprocal.bin'),
                            ('dok_ui_panel_bank','panels.bin'),('dok_ui_panel_offsets','panel-offsets.bin'),
                            ('dok_hw_ui_panel_bank','hw-panels.bin'),('dok_hw_ui_panel_offsets','hw-panel-offsets.bin'),
                            ('dok_hw_bg_bank','hw-bg-bank.bin'),('dok_hw_bg_counts','hw-bg-counts.bin'),
                            ('dok_hw_bg_objects','hw-bg-objects.bin'),('dok_hw_bg_animated','hw-bg-animated.bin'),
                            ('dok_hw_bg_maps','hw-bg-maps.bin'),('dok_hw_bg_offsets','hw-bg-offsets.bin'),
                            ('dok_hw_bg_distances','hw-bg-distances.bin'),
                            ('dok_hw_bg_row_matrix','hw-bg-row-matrix.bin'),('dok_hw_bg_row_cnt','hw-bg-row-cnt.bin')):
        assembly += [f'.section .rodata.{symbol},"a",%progbits','.balign 4',f'.global {symbol}',f'{symbol}:',f'.incbin "{(out/filename).resolve().as_posix()}"']
    assembly+=['.section .rodata.dok_hw_obj_row_oam,"a",%progbits','.balign 4','.global dok_hw_obj_row_oam','dok_hw_obj_row_oam:',
               '.incbin "'+(out/'hw-obj-row-oam.bin').resolve().as_posix()+'"']
    assembly += ['.section .rodata.dok_bsp_padding,"a",%progbits','.balign 4','.global dok_bsp_padding','dok_bsp_padding:', '.short '+','.join(map(str,padding))]
    assembly += ['.section .rodata.dok_ui_panel_sources,"a",%progbits','.balign 4','.global dok_ui_panel_sources','dok_ui_panel_sources:']
    assembly += ['.word dok_gba_assets+'+str(offset) for offset in panel_sources]
    if include_lowered:
        for symbol,filename in (('dok_hw_low_maps','hw-low-maps.bin'),('dok_hw_low_row_matrix','hw-low-row-matrix.bin'),('dok_hw_low_row_cnt','hw-low-row-cnt.bin')):
            assembly+=['.section .rodata.'+symbol+',"a",%progbits','.balign 4','.global '+symbol,symbol+':','.incbin "'+(out/filename).resolve().as_posix()+'"']
    (out/'tables.S').write_text('\n'.join(assembly)+'\n',encoding='utf-8')
    return dict(package_sha256=hashlib.sha256(data).hexdigest(), nodes_per_view=8191,
                bsp_bytes=len(bounds)*4,padding=padding,prelit_bytes=len(bank),
                columns_no_scales_bytes=len(columns),stored_light_rows=list(range(16)),
                quantized_light_rows=[0,4,8,12,15],exact_hud_panel_atlas_bytes=len(panel_bank),
                surface_side_columns=include_sides,entity_columns=include_entities,empty_column_metadata=include_empty,hardware_bg=hardware_bg)


@clean_objects_after_build
def build(base,out,variants):
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    out.mkdir(parents=True,exist_ok=True)
    streamed_rows={96 if 'HW_ROWS96' in VARIANTS[v] else 72 if 'HW_ROWS72' in VARIANTS[v] else 128 for v in variants if 'HW_UI_OBJ' in VARIANTS[v]}
    assert len(streamed_rows)<=1,'Build differing HBlank scene row layouts in separate output directories'
    metadata=tables(out,ROOT/'build/gba/assets/assets.bin',any('SIDES_PRELIT' in VARIANTS[v] for v in variants),
                    any('HW_SPAN_SOLID' in VARIANTS[v] for v in variants),
                    any('HW_ENTITY_PRELIT' in VARIANTS[v] for v in variants),
                    any('HW_BG_LOWERED' in VARIANTS[v] for v in variants),
                    any('HW_ALPHA_EMPTY' in VARIANTS[v] for v in variants),next(iter(streamed_rows),128))
    startup=['cc','-target','arm-freestanding-eabi','-mcpu=arm7tdmi',
             '-DDOK_RENDERER_PROBE','-DDOK_RENDER_HALF_COLUMNS','-c']
    command([zig,*startup,out/'tables.S','-o',out/'tables.o'])
    if any('HW_SPARSE_POSTS' in VARIANTS[v] for v in variants):
        from gba_sparse_posts import build_posts
        metadata['sparse_posts']=build_posts(out,ROOT/'build/gba/assets/assets.bin')
        command([zig,*startup,out/'sparse-posts.S','-o',out/'sparse-posts.o'])
    stream_metadata=None
    sector_metadata=None
    if any('SECTOR_CLIP' in VARIANTS[v] for v in variants):
        from gba_sector_scene import sector_scene_tables
        sector_metadata=sector_scene_tables(out,(ROOT/'build/gba/assets/assets.bin').read_bytes())
        command([zig,*startup,out/'sector-tables.S','-o',out/'sector-tables.o'])
    world_obj_metadata=None
    sector_gpu_metadata=None
    if any('SECTOR_GPU' in VARIANTS[v] for v in variants):
        from gba_sector_gpu_assets import sector_gpu_tables
        sector_gpu_metadata=sector_gpu_tables(out,(ROOT/'build/gba/assets/assets.bin').read_bytes())
        command([zig,*startup,out/'sector-gpu-tables.S','-o',out/'sector-gpu-tables.o'])
    if any('HW_WORLD_OBJ' in VARIANTS[v] for v in variants):
        from gba_world_obj_assets import build_world_obj_tables
        world_obj_metadata=build_world_obj_tables(out,(ROOT/'build/gba/assets/assets.bin').read_bytes())
        command([zig,*startup,out/'world-obj-tables.S','-o',out/'world-obj-tables.o'])
    if any('STREAM_BSP' in VARIANTS[v] for v in variants):
        from gba_stream_scene import runtime_scene_tables
        stream_metadata=runtime_scene_tables(out,(ROOT/'build/gba/assets/assets.bin').read_bytes())
        command([zig,*startup,out/'stream-tables.S','-o',out/'stream-tables.o'])
    motion=('flow flow_backend ui ui_backend audio sound audio_backend ai ai_probe '
            'combat combat_probe items movement collision interactions movement_probe '
            'movement_cases collision_cases interactions_cases').split()
    names=['startup','main','ui-icons','animation-index','render-tables','divmod','columns']
    names+=['motion-'+n for n in motion]
    names+=['time-scheduler','time-clock','gba-clock','time-time_probe','time-time_cases']
    names+=['memory','runtime','rom_assets','projection','image','animation','visual','renderer','assets-data']
    flags=startup[:-1]+['-std=c11','-Wall','-Wextra','-Werror','-O2','-g','-ffreestanding',
                        '-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
                        '-fno-pic','-I','include','-DDOK_RUNTIME_ROM_ONLY','-c']
    result={}
    for variant in variants:
        dest=out/variant; dest.mkdir(parents=True,exist_ok=True)
        defines=VARIANTS[variant]
        objects=[base/(n+'.o') for n in names]
        # Full private source snapshot preserves exact experiment inputs.
        private=dest/'source';private.mkdir(exist_ok=True)
        (private/'dok').mkdir(exist_ok=True)
        (private/'dok/gba_hw_scratch.h').write_bytes((ROOT/'include/dok/gba_hw_scratch.h').read_bytes())
        (private/'dok/gba_world_obj.h').write_bytes((ROOT/'include/dok/gba_world_obj.h').read_bytes())
        (private/'dok/gba_sector_gpu.h').write_bytes((ROOT/'include/dok/gba_sector_gpu.h').read_bytes())
        for p in (ROOT/'src').glob('renderer*'):
            (private/p.name).write_bytes(p.read_bytes())
        if 'EXACT_LIGHT' in defines:
            p=private/'renderer.c'
            p.write_text(p.read_text(encoding='utf-8').replace('dok_prelit_offsets','dok_exact_prelit_offsets'),encoding='utf-8')
        if 'RAM_GEOMETRY' in defines:
            p=private/'renderer.c'
            s=p.read_text(encoding='utf-8')
            s=s.replace('#define DOK_COLD __attribute__((noinline,section(".rom.text")))',
                        '#define DOK_COLD __attribute__((noinline))')
            p.write_text(s,encoding='utf-8')
        if 'LOW_VERTICAL' in defines:
            # Isolated quality experiment: 120x72 scene, original-resolution UI.
            for p in private.glob('renderer*'):
                if p.suffix not in ('.c','.inc'):continue
                s=p.read_text(encoding='utf-8')
                if p.name=='renderer.c':
                    s=s.replace('y*9/10:-(((-y)*9+9)/10)', 'y*9/20:-(((-y)*9+19)/20)')
                    s=s.replace('DOK_RENDER_SCENE_HEIGHT','72')
                    # Scene sentinels only; fixed row pitch and UI ABI stay240x160.
                    s=s.replace('y<144','y<72').replace('y<144?y+n:144','y<72?y+n:72')
                    s=s.replace('))<144', '))<72')
                    s=s.replace('y+n<144?y+n:144','y+n<72?y+n:72').replace('return 144;','return 72;')
                    s=s.replace('run=144','run=72').replace('run==144','run==72').replace('run!=144','run!=72')
                    s=s.replace('COVER(w,x,k)[0]:144','COVER(w,x,k)[0]:72')
                    s=s.replace('STAGE(4);status=DOK_RENDER_OK;', '''for(int y=143;y>=0;--y) {
        uint32_t *dst=w->frame_words+y*60;
        const uint32_t *src=w->frame_words+(y/2)*60;
        for(unsigned x=0;x<60;++x) dst[x]=src[x];
    }
    STAGE(4);status=DOK_RENDER_OK;''')
                if p.name not in ('renderer.c','renderer_trig.inc'):
                    s=s.replace('143','71')
                if p.name in ('renderer_surface_raster.inc','renderer_surface_raster_body.inc','renderer_surface_sides.inc','renderer_surfaces.inc','renderer_bsp.inc'):
                    s=s.replace('144','72')
                p.write_text(s,encoding='utf-8')
        if 'HW_VERTICAL128' in defines:
            rows=96 if 'HW_ROWS96' in defines else 72 if 'HW_ROWS72' in defines else 128
            numerator,denominator={128:(4,5),96:(3,5),72:(9,20)}[rows]
            step=144//(rows//8);pd=256*8//step
            for name in ('renderer.c','renderer_surfaces.inc','renderer_surface_sides.inc','renderer_surface_raster.inc','renderer_surface_raster_body.inc','renderer_bsp.inc'):
                p=private/name;s=p.read_text(encoding='utf-8')
                if name=='renderer.c':
                    old='y*9/10:-(((-y)*9+9)/10)';assert old in s
                    s=s.replace(old,f'y*{numerator}/{denominator}:-(((-y)*{numerator}+{denominator-1})/{denominator})').replace('DOK_RENDER_SCENE_HEIGHT',str(rows))
                    s=s.replace('while(y<144)',f'while(y<{rows})').replace('))<144',f'))<{rows}')
                    s=s.replace('y+n<144?y+n:144',f'y+n<{rows}?y+n:{rows}').replace('return 144;',f'return {rows};')
                    s=s.replace('run=144',f'run={rows}').replace('run==144',f'run=={rows}').replace('run!=144',f'run!={rows}')
                    s=s.replace('COVER(w,x,k)[0]:144',f'COVER(w,x,k)[0]:{rows}').replace('0,143',f'0,{rows-1}')
                    s=s.replace('i<18432/4',f'i<{rows*128}/4')
                    anchor='STAGE(4);status=DOK_RENDER_OK;';assert anchor in s
                    source_expr='(unsigned)y/9*8+(unsigned)y%9-((unsigned)y%9>=5)' if rows==128 else f'(unsigned)y/{step}*8+(unsigned)((((int)y%{step}-{step//2})*{pd}+1024)>>8)'
                    if rows==96:source_expr=f'((unsigned)y%12==0 && y)?(unsigned)y/12*8-1:({source_expr})'
                    s=s.replace(anchor,'''if(!g_hw_tile_active) for(int y=143;y>=0;--y) {
        unsigned source='''+source_expr+''';
        uint32_t *dst=w->frame_words+y*60;
        const uint32_t *src=w->frame_words+source*60;
        for(unsigned x=0;x<60;++x)dst[x]=src[x];
    }
    STAGE(4);status=DOK_RENDER_OK;''')
                else:
                    s=s.replace('143',str(rows-1)).replace('144',str(rows))
                    if name=='renderer_bsp.inc':
                        s=s.replace('int top=71+',f'int top={rows//2-1}+').replace('int bottom=71+',f'int bottom={rows//2-1}+').replace('*180*',f'*{rows*5//4}*')
                p.write_text(s,encoding='utf-8')
        compile_flags=[f.replace('arm-freestanding-eabi','thumb-freestanding-eabi') for f in flags] if 'THUMB_RENDERER' in defines else flags
        command([zig,*compile_flags,*['-DDOK_RENDER_'+d+'_PROTOTYPE' for d in defines],
                 private/'renderer.c','-o',dest/'renderer.o'])
        objects[names.index('renderer')]=dest/'renderer.o'
        if 'HW_PROJECT_RECIP' in defines:
            assert 'EXACT_PLANES' in defines
            (private/'projection.c').write_bytes((ROOT/'src/projection.c').read_bytes())
            command([zig,*flags,'-DDOK_PROJECTION_RECIP',private/'projection.c','-o',dest/'projection.o'])
            objects[names.index('projection')]=dest/'projection.o'
        if 'HW_COVER4' in defines or 'HW_LINEAR_ROM' in defines:
            assert 'HW_COVER4' not in defines or 'HW_VERTICAL128' in defines
            assert 'HW_LINEAR_ROM' not in defines or 'HW_DIRECT' in defines
            (private/'columns.S').write_bytes((ROOT/'gba/columns.S').read_bytes())
            linear_name='fallback-samplers.o' if 'HW_LINEAR_ROM' in defines else 'columns.o'
            linear_flags=['-DDOK_HW_COVER4'] if 'HW_COVER4' in defines else []
            command([zig,*startup,*linear_flags,private/'columns.S','-o',dest/linear_name])
            objects[names.index('columns')]=dest/linear_name
        if 'HW_OBJ' in defines or 'MAIN_CONTROL' in defines:
            for name in ('renderer_probe.c','hardware_obj.inc','hardware_direct.inc','hardware_bg.inc','hardware_ui_obj.inc','hardware_world_obj_probe.inc','hardware_world_obj.inc','hardware_sector_bitmap.inc','hardware_sector_gpu.inc'):
                (private/name).write_bytes((ROOT/'gba'/name).read_bytes())
            main_flags=[f.replace('arm-freestanding-eabi','thumb-freestanding-eabi') for f in flags]
            main_defines=['-DDOK_RENDER_HW_OBJ_PROTOTYPE'] if 'HW_OBJ' in defines else []
            if 'HW_PACK_ARM' in defines:main_defines+=['-DDOK_RENDER_HW_PACK_ARM_PROTOTYPE']
            if 'HW_DIRECT' in defines:main_defines+=['-DDOK_RENDER_HW_DIRECT_PROTOTYPE']
            if 'HW_BG' in defines:main_defines+=['-DDOK_RENDER_HW_BG_PROTOTYPE']
            if 'HW_RAYS32' in defines:main_defines+=['-DDOK_RENDER_HW_RAYS32_PROTOTYPE']
            if 'HW_ROW_LUT' in defines:main_defines+=['-DDOK_RENDER_HW_ROW_LUT_PROTOTYPE']
            if 'HW_BG_STREAM' in defines:main_defines+=['-DDOK_RENDER_HW_BG_STREAM_PROTOTYPE']
            if 'HW_UI_REUSE' in defines:main_defines+=['-DDOK_RENDER_HW_UI_REUSE_PROTOTYPE']
            if 'HW_PRESENT' in defines:main_defines+=['-DDOK_RENDER_HW_PRESENT_PROTOTYPE']
            if 'HW_MAP_CACHE' in defines:main_defines+=['-DDOK_RENDER_HW_MAP_CACHE_PROTOTYPE']
            if 'HW_EARLY_PRESENT' in defines:main_defines+=['-DDOK_RENDER_HW_EARLY_PRESENT_PROTOTYPE']
            if 'HW_VERTICAL128' in defines:main_defines+=['-DDOK_RENDER_HW_VERTICAL128_PROTOTYPE']
            if 'HW_OBJ_ROWS_PROBE' in defines:main_defines+=['-DDOK_RENDER_HW_OBJ_ROWS_PROBE_PROTOTYPE']
            if 'HW_WORLD_OBJ_PROBE' in defines:main_defines+=['-DDOK_RENDER_HW_WORLD_OBJ_PROBE_PROTOTYPE']
            if 'HW_WORLD_OBJ' in defines:main_defines+=['-DDOK_RENDER_HW_WORLD_OBJ_PROTOTYPE']
            if 'HW_UI_OBJ' in defines:main_defines+=['-DDOK_RENDER_HW_UI_OBJ_PROTOTYPE']
            if 'HW_BG_LOWERED' in defines:main_defines+=['-DDOK_RENDER_HW_BG_LOWERED_PROTOTYPE']
            if 'HW_OAM_RAM' in defines:main_defines+=['-DDOK_RENDER_HW_OAM_RAM_PROTOTYPE']
            if 'HW_LOW_ADAPTIVE' in defines:main_defines+=['-DDOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE']
            if 'HW_ROWS96' in defines:main_defines+=['-DDOK_RENDER_HW_ROWS96_PROTOTYPE']
            if 'HW_ROWS72' in defines:main_defines+=['-DDOK_RENDER_HW_ROWS72_PROTOTYPE']
            if 'SECTOR_BITMAP' in defines:main_defines+=['-DDOK_RENDER_SECTOR_BITMAP_PROTOTYPE']
            if 'SECTOR_GPU' in defines:main_defines+=['-DDOK_RENDER_SECTOR_GPU_PROTOTYPE']
            if 'SECTOR_GPU_SHEAR' in defines:main_defines+=['-DDOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE']
            command([zig,*main_flags,*main_defines,private/'renderer_probe.c','-o',dest/'main.o'])
            objects[names.index('main')]=dest/'main.o'
        if 'HW_BG' in defines:
            (private/'startup.S').write_bytes((ROOT/'gba/startup.S').read_bytes())
            command([zig,*startup,'-DDOK_RENDER_HW_BG_PROTOTYPE',private/'startup.S','-o',dest/'startup.o'])
            objects[names.index('startup')]=dest/'startup.o'
        if 'HW_DIRECT' in defines:
            (private/'combat_probe.c').write_bytes((ROOT/'gba/combat_probe.c').read_bytes())
            combat_flags=[f.replace('arm-freestanding-eabi','thumb-freestanding-eabi') for f in flags]
            command([zig,*combat_flags,'-DDOK_RENDER_HW_DIRECT_PROTOTYPE',private/'combat_probe.c','-o',dest/'motion-combat_probe.o'])
            objects[names.index('motion-combat_probe')]=dest/'motion-combat_probe.o'
            tiled_flags=['-DDOK_HW_SURFACE_PAIR'] if 'HW_CEILING2' in defines else []
            if 'HW_MASKED' in defines:tiled_flags+=['-DDOK_HW_TILED_MASKED']
            if 'HW_SURFACE_PRELIT' in defines:tiled_flags+=['-DDOK_HW_SURFACE_PRELIT']
            if 'HW_COVER4' in defines:tiled_flags+=['-DDOK_HW_COVER4']
            if 'HW_NONZERO_COLUMN' in defines:tiled_flags+=['-DDOK_HW_NONZERO_COLUMN']
            if 'HW_COMPACT_ZERO' in defines:tiled_flags+=['-DDOK_HW_COMPACT_ZERO']
            if 'HW_MASKED_BATCH' in defines:tiled_flags+=['-DDOK_HW_MASKED_BATCH']
            if 'HW_OPAQUE_BATCH' in defines:tiled_flags+=['-DDOK_HW_OPAQUE_BATCH']
            if 'HW_BG_LOWERED' in defines:tiled_flags+=['-DDOK_HW_BG_LOWERED']
            if 'SECTOR_GPU' in defines:tiled_flags+=['-DDOK_SECTOR_GPU']
            command([zig,*startup,*tiled_flags,ROOT/'gba/tiled_scene.S','-o',dest/'tiled-columns.o'])
            objects.append(dest/'tiled-columns.o')
        if 'UI_CACHE' in defines or 'UI_CONTROL' in defines:
            for name in ('ui.c','ui_text.inc','ui_native_text.inc','ui_panel_cache.inc','ui_tiled.inc'):
                (private/name).write_bytes((ROOT/'src'/name).read_bytes())
            p=private/'ui.c';s=p.read_text(encoding='utf-8')
            if 'UI_CACHE' in defines:
                s=s.replace('static const dok_ui_art *art;', 'static const dok_ui_art *art;\n#include "ui_panel_cache.inc"')
                s=s.replace('static void panel(canvas *c,unsigned x,unsigned y,unsigned w,unsigned h){',
                            'static void panel(canvas *c,unsigned x,unsigned y,unsigned w,unsigned h){\n if(cached_panel(c,x,y,w,h))return;')
            p.write_text(s,encoding='utf-8')
            ui_flags=[f.replace('arm-freestanding-eabi','thumb-freestanding-eabi') for f in flags]
            ui_defines=['-DDOK_RENDER_HW_DIRECT_PROTOTYPE'] if 'HW_DIRECT' in defines else []
            if 'HW_UI_REUSE' in defines:ui_defines+=['-DDOK_RENDER_HW_UI_REUSE_PROTOTYPE']
            command([zig,*ui_flags,*ui_defines,p,'-o',dest/'motion-ui.o'])
            objects[names.index('motion-ui')]=dest/'motion-ui.o'
            command([zig,*startup,ROOT/'gba/ui_panels.S','-o',dest/'ui-columns.o'])
            objects.append(dest/'ui-columns.o')
        if 'NO_SCALES' in defines:
            assembly=(base/'assets-data.S').read_text(encoding='utf-8')
            assembly=assembly.replace((base/'render-columns.bin').resolve().as_posix(),(out/'columns.bin').resolve().as_posix())
            assert (out/'columns.bin').resolve().as_posix() in assembly
            (dest/'assets-data.S').write_text(assembly,encoding='utf-8')
            command([zig,*startup,dest/'assets-data.S','-o',dest/'assets-data.o'])
            objects[names.index('assets-data')]=dest/'assets-data.o'
        if variant not in ('baseline','no_scales'):
            objects.append(out/'tables.o')
        if 'HW_SPARSE_POSTS' in defines:objects.append(out/'sparse-posts.o')
        if 'STREAM_BSP' in defines:objects.append(out/'stream-tables.o')
        if 'SECTOR_CLIP' in defines:
            objects.append(out/'sector-tables.o')
            (private/'sector_planes.S').write_bytes((ROOT/'gba/sector_planes.S').read_bytes())
            sector_flags=['-DDOK_SECTOR_BITMAP'] if 'SECTOR_BITMAP' in defines else []
            command([zig,*startup,*sector_flags,private/'sector_planes.S','-o',dest/'sector-planes-columns.o'])
            objects.append(dest/'sector-planes-columns.o')
            if 'SECTOR_BITMAP' in defines:
                (private/'sector_bitmap.S').write_bytes((ROOT/'gba/sector_bitmap.S').read_bytes())
                command([zig,*startup,private/'sector_bitmap.S','-o',dest/'sector-bitmap-columns.o'])
                objects.append(dest/'sector-bitmap-columns.o')
        if 'HW_WORLD_OBJ' in defines:objects.append(out/'world-obj-tables.o')
        if 'SECTOR_GPU' in defines:
            objects.append(out/'sector-gpu-tables.o')
            (private/'sector_gpu.S').write_bytes((ROOT/'gba/sector_gpu.S').read_bytes())
            command([zig,*startup,private/'sector_gpu.S','-o',dest/'sector-gpu-columns.o'])
            objects.append(dest/'sector-gpu-columns.o')
        if 'PROCEDURAL' in defines:
            # Use a columns.o suffix so the established linker places this
            # tiny hot kernel in IWRAM alongside the other ARM samplers.
            command([zig,*startup,ROOT/'gba/procedural.S','-o',dest/'procedural-columns.o'])
            objects.append(dest/'procedural-columns.o')
        if 'EXACT_PLANES' in defines:
            plane_flags=['-DDOK_RENDER_RASTER_DIV_PROTOTYPE'] if 'RASTER_DIV' in defines else []
            if 'HW_U_RECIP' in defines:plane_flags+=['-DDOK_GBA_U_RECIP']
            if 'HW_U_WIDE' in defines:plane_flags+=['-DDOK_GBA_U_WIDE']
            command([zig,*startup,*plane_flags,ROOT/'gba/planes.S','-o',dest/'planes-columns.o'])
            objects.append(dest/'planes-columns.o')
        if 'WORLD_SPANS' in defines:
            water_flags=['-DDOK_RENDER_WATER_SPANS_PROTOTYPE'] if 'WATER_SPANS' in defines else []
            if 'COARSE_PLANES' in defines:water_flags+=['-DDOK_RENDER_COARSE_PLANES_PROTOTYPE']
            command([zig,*startup,*water_flags,ROOT/'gba/world_spans.S','-o',dest/'world-columns.o'])
            objects.append(dest/'world-columns.o')
        if 'MASKED_ARM' in defines:
            command([zig,*startup,ROOT/'gba/masked_columns.S','-o',dest/'masked-columns.o'])
            objects.append(dest/'masked-columns.o')
        if 'WORLD_LOOKUP' in defines:
            command([zig,*startup,ROOT/'gba/world_lookup.S','-o',dest/'lookup-columns.o'])
            objects.append(dest/'lookup-columns.o')
        if 'HW_PACK_ARM' in defines:
            command([zig,*startup,ROOT/'gba/hw_obj_pack.S','-o',dest/'hw-obj-columns.o'])
            objects.append(dest/'hw-obj-columns.o')
        if 'HW_ROM_COLUMN_CACHE' in defines:
            command([zig,*startup,ROOT/'gba/column_cache.S','-o',dest/'cache-columns.o'])
            objects.append(dest/'cache-columns.o')
        if 'HW_STATIC_GEOMETRY' in defines or 'STREAM_BSP' in defines or 'SECTOR_CLIP' in defines:
            command([zig,*startup,ROOT/'gba/geometry_cache.S','-o',dest/'geometry-cache-columns.o'])
            objects.append(dest/'geometry-cache-columns.o')
        if 'FAST_OPAQUE_ARM' in defines or 'HW_DIRECT_GAPS' in defines:
            opaque_flags=['-DDOK_OPAQUE_GAPS'] if 'OPAQUE_GAPS' in defines or 'HW_DIRECT_GAPS' in defines else []
            command([zig,*startup,*opaque_flags,ROOT/'gba/opaque_tiled.S','-o',dest/'opaque-columns.o'])
            objects.append(dest/'opaque-columns.o')
        command([zig,'ld.lld','-T','gba/gba.ld','--gc-sections','--defsym=__iwram_reserved_override=8192',
                 '-Map='+str(dest/'dok-diagnostic.map'),*objects,'-o',dest/'dok-diagnostic.elf'])
        command([zig,'objcopy','-O','binary',dest/'dok-diagnostic.elf',dest/'dok-diagnostic.gba'])
        command([sys.executable,'scripts/gba_rom.py','--elf',dest/'dok-diagnostic.elf',
                 '--rom',dest/'dok-diagnostic.gba','--out',dest/'build-report.json'])
        result[variant]=json.loads((dest/'build-report.json').read_text(encoding='utf-8'))
        result[variant]['prototype_defines']=list(defines)
        print('Built '+variant,flush=True)
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    manifest=dict(tables=metadata,stream_tables=stream_metadata,sector_tables=sector_metadata,sector_gpu_tables=sector_gpu_metadata,world_obj_tables=world_obj_metadata,variants=result,base_rom_sha256=digest(base/'dok-diagnostic.gba'),
                  base_objects={n:digest(base/(n+'.o')) for n in names},
                  sources={p.as_posix():digest(p) for p in [*Path('src').glob('renderer*'),Path('src/projection.c'),
                           *[Path('src')/name for name in ('ui.c','ui_text.inc','ui_native_text.inc','ui_panel_cache.inc','ui_tiled.inc')],
                           *[Path('gba')/name for name in ('columns.S','column_cache.S','geometry_cache.S','planes.S','procedural.S','world_spans.S','ui_panels.S','masked_columns.S','world_lookup.S','renderer_probe.c','hardware_obj.inc','hardware_direct.inc','hardware_bg.inc','hardware_ui_obj.inc','hw_obj_pack.S','combat_probe.c','tiled_scene.S','startup.S','opaque_tiled.S')],
                           Path('gba/hardware_world_obj_probe.inc'),Path('gba/hardware_world_obj.inc'),Path('include/dok/gba_world_obj.h'),
                           Path('scripts/gba_world_obj_assets.py'),Path('scripts/gba_sector_scene.py'),Path('gba/sector_planes.S'),Path('gba/sector_bitmap.S'),Path('gba/hardware_sector_bitmap.inc'),Path(__file__),Path('scripts/gba_hw_bg_assets.py'),Path('scripts/compile_gba_render_scene.py'),
                           Path('scripts/gba_sector_gpu_assets.py'),Path('gba/sector_gpu.S'),Path('gba/hardware_sector_gpu.inc'),Path('include/dok/gba_sector_gpu.h'),
                           Path('scripts/gba_stream_scene.py'),Path('include/dok/gba_hw_scratch.h'),
                           Path('scripts/gba_sparse_posts.py')]})
    (out/'prototype-build.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,default=Path('build/select-menu-fix'))
    p.add_argument('--out',type=Path,default=Path('build/g20a-prototypes'))
    p.add_argument('--variants',nargs='+',choices=VARIANTS,default=list(VARIANTS))
    p.add_argument('--keep-objects',action='store_true',help='Retain experiment object files for manual relinking')
    a=p.parse_args();os.chdir(ROOT)
    os.environ['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'build/zig-cache')
    build(a.base,a.out,a.variants,keep_objects=a.keep_objects)


if __name__=='__main__':main()
