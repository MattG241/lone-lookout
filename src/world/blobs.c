/*
 * blobs.c -- the baked world data, embedded in the .dol by bin2o.
 */
#include "world/blobs.h"
#include "assets.h"

blob_t world_blob(blob_id id)
{
	switch (id) {
	case BLOB_HEIGHT: return (blob_t){ world_height_bin, world_height_bin_size };
	case BLOB_COLOR: return (blob_t){ world_color_bin, world_color_bin_size };
	case BLOB_SURFACE: return (blob_t){ world_surface_bin, world_surface_bin_size };
	case BLOB_TREES: return (blob_t){ world_trees_bin, world_trees_bin_size };
	case BLOB_TUFTS: return (blob_t){ world_tufts_bin, world_tufts_bin_size };
	default: return (blob_t){ 0, 0 };
	}
}
