/**
 * Copyright (c) 2011 ~ 2013 Deepin, Inc.
 *               2011 ~ 2013 Long Wei
 *
 * Author:      Long Wei <yilang2007lw@gmail.com>
 * Maintainer:  Long Wei <yilang2007lw@gamil.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 **/

#include <parted/parted.h>
#include <mntent.h>
#include <sys/mount.h>
#include "part_util.h"
#include "fs_util.h"
#include "ped_utils.h"
#include "info.h"
#include <math.h>


static GHashTable *disks;
static GHashTable *partitions;
static GHashTable *disk_partitions;
static GHashTable *partition_os = NULL;
static GHashTable *partition_os_desc = NULL;
void mkfs_latter(const char* path, const char* fs);

JS_EXPORT_API 
gchar* installer_rand_uuid (const char* prefix)
{
    gchar *result = NULL;

    static gint32 number = 0;
    result = g_strdup_printf("%s%i", prefix, number++);

    return result;
}

static gpointer 
thread_os_prober (gpointer data)
{
    partition_os = g_hash_table_new_full ( g_str_hash, g_str_equal, 
                                           g_free, g_free);

    partition_os_desc = g_hash_table_new_full ( g_str_hash, g_str_equal, 
                                           g_free, g_free);

    gchar *cmd = g_find_program_in_path ("os-prober");
    if (cmd == NULL) {
        g_warning ("os:os-prober not installed\n");
    }
    g_spawn_command_line_sync ("pkill -9 os-prober", NULL, NULL, NULL, NULL);

    gchar *output = NULL;
    GError *error = NULL;
    g_spawn_command_line_sync (cmd, &output, NULL, NULL, &error);
    if (error != NULL) {
        g_warning ("get partition os:os-prober %s\n", error->message);
        g_error_free (error);
        error = NULL;
    }

    gchar **items = g_strsplit (output, "\n", -1);
    int i, j;
    for (i = 0; i < g_strv_length (items); i++) {
        gchar *item = g_strdup (items[i]);
        gchar **os = g_strsplit (item, ":", -1);

        if (g_strv_length (os) == 4) {
            g_hash_table_insert (partition_os, g_strdup (os[0]), g_strdup (os[2]));
            g_hash_table_insert (partition_os_desc, g_strdup (os[0]), g_strdup (os[1]));
        }

        g_strfreev (os);
        g_free (item);
    }
    g_strfreev (items);
    g_free (output);
    g_free (cmd);

    GRAB_CTX ();
    js_post_message ("os_prober", NULL);
    UNGRAB_CTX ();
    return NULL;
}



static
gboolean is_freespace_and_smaller_than_10MB(PedPartition* part)
{
    if (part->type & PED_PARTITION_FREESPACE == 0)
	return FALSE;
    int sector_size = part->disk->dev->sector_size;
    return part->geom.length * sector_size < 10 * 1024 * 1024;
}

GList* build_part_list(PedDisk* disk)
{
    GList *part_list = NULL;

    PedPartition *part = NULL;
    for (part = ped_disk_next_partition (disk, NULL); part; part = ped_disk_next_partition (disk, part)) {
	if ((part->type & PED_PARTITION_METADATA) == PED_PARTITION_METADATA) {
	    continue;
	}
	if (is_freespace_and_smaller_than_10MB(part)) {
	    continue;
	}

	gchar *part_uuid = installer_rand_uuid ("part");
	part_list = g_list_append (part_list, g_strdup (part_uuid));
	g_hash_table_insert (partitions, g_strdup (part_uuid), part);
	g_free (part_uuid);
    }
    return part_list;
}

const PedDiskType* best_disk_type()
{
    if (installer_system_support_efi()) {
	return ped_disk_type_get("gpt");
    } else {
	return ped_disk_type_get("msdos");
    }
}

PedDisk* try_build_disk(PedDevice* device)
{
    if (device->read_only) {
	return NULL;
    }
    PedDiskType* type = ped_disk_probe(device);
    if (type == NULL) {
	return ped_disk_new_fresh(device, best_disk_type());
    } else if (strncmp(type->name, "gpt", 3) != 0 && strncmp(type->name, "msdos", 5) != 0) {
	//filter other type of disks, like raid's partition type = "loop"
	return NULL;
    } else {
	return ped_disk_new (device);
    }
}


static gpointer thread_init_parted (gpointer data)
{
    ped_device_probe_all ();

    PedDevice *device = NULL;
    while ((device = ped_device_get_next (device))) {
	PedDisk* disk = try_build_disk(device);
	if (disk == 0) {
	    continue;
	}

        gchar *uuid = installer_rand_uuid("disk"); 
	g_hash_table_insert (disks, g_strdup (uuid), disk);

        g_hash_table_insert (disk_partitions, g_strdup (uuid), build_part_list(disk));
        g_free (uuid);
    }

    GRAB_CTX ();
    js_post_message ("init_parted", NULL);
    UNGRAB_CTX ();

    return NULL;
}

void init_parted ()
{
    disks = g_hash_table_new_full ((GHashFunc) g_str_hash, 
                                   (GEqualFunc) g_str_equal, 
                                   (GDestroyNotify) g_free, 
                                   NULL);

    disk_partitions = g_hash_table_new_full ((GHashFunc) g_str_hash, 
                                             (GEqualFunc) g_str_equal, 
                                             (GDestroyNotify) g_free, 
                                             (GDestroyNotify) g_list_free);

    partitions = g_hash_table_new_full ((GHashFunc) g_str_hash, 
                                        (GEqualFunc) g_str_equal, 
                                        (GDestroyNotify) g_free, 
                                        NULL);


    GThread *thread = g_thread_new ("init-parted", (GThreadFunc) thread_init_parted, NULL);
    g_thread_unref (thread);

    GThread *prober_thread = g_thread_new ("os-prober", (GThreadFunc) thread_os_prober, NULL);
    g_thread_unref (prober_thread);
}

JS_EXPORT_API 
JSObjectRef installer_list_disks()
{
    g_return_val_if_fail(disks != NULL, json_array_create());

    JSObjectRef array = json_array_create ();
    GList *disk_keys = g_hash_table_get_keys (disks);
    for (int i = 0; i < g_list_length (disk_keys); i++) {
        json_array_insert (array, i, jsvalue_from_cstr (get_global_context(), g_list_nth_data (disk_keys, i)));
    }
    g_list_free(disk_keys);

    return array;
}

JS_EXPORT_API
gchar *installer_get_disk_path (const gchar *uuid)
{
    g_return_val_if_fail(uuid != NULL, g_strdup("Unknow"));

    PedDisk* disk = (PedDisk *) g_hash_table_lookup (disks, uuid);
    g_return_val_if_fail(disk != NULL, g_strdup("Unknow"));

    return g_strdup (disk->dev->path);
}

JS_EXPORT_API
gchar *installer_get_disk_type (const gchar *uuid)
{
    g_return_val_if_fail(uuid != NULL, g_strdup("Unknow"));

    PedDisk* peddisk = (PedDisk *) g_hash_table_lookup (disks, uuid);
    g_return_val_if_fail(peddisk != NULL, g_strdup("Unknow"));

    PedDiskType* type = ped_disk_probe (peddisk->dev);
    g_return_val_if_fail(type != NULL, g_strdup("Unknow"));
    return g_strdup (type->name);
}

JS_EXPORT_API
gchar *installer_get_disk_model (const gchar *uuid)
{
    g_return_val_if_fail(uuid != NULL, g_strdup("Unknow"));
    PedDisk* disk = (PedDisk *) g_hash_table_lookup(disks, uuid);
    g_return_val_if_fail(disk != NULL, g_strdup("Unknow"));

    return g_strdup (disk->dev->model);
}

JS_EXPORT_API
double installer_get_disk_max_primary_count (const gchar *uuid)
{
    g_return_val_if_fail(uuid != NULL, 0);
    PedDisk* disk = (PedDisk *) g_hash_table_lookup (disks, uuid);
    g_return_val_if_fail(disk != NULL, 0);

    return ped_disk_get_max_primary_partition_count (disk);
}

JS_EXPORT_API
double installer_get_disk_size (const gchar *uuid)
{
    g_return_val_if_fail(uuid != NULL, 0);
    PedDisk *disk = (PedDisk *) g_hash_table_lookup(disks, uuid);
    g_return_val_if_fail(disk != NULL, 0);

    return disk->dev->length * disk->dev->sector_size;
}

JS_EXPORT_API
JSObjectRef installer_get_disk_partitions (const gchar *disk)
{
    GRAB_CTX ();
    JSObjectRef array = json_array_create ();
    int i;
    GList *parts = NULL;
    if (disk == NULL) {
        g_warning ("get disk partitions:disk NULL\n");
    } else {
        parts = (GList *) g_hash_table_lookup (disk_partitions, disk);
    }
   
    if (parts != NULL) {
        for (i = 0; i < g_list_length (parts); i++) {
            json_array_insert (array, i, jsvalue_from_cstr (get_global_context(), g_list_nth_data (parts, i)));
        }
    }

    UNGRAB_CTX ();
    return array;
}

JS_EXPORT_API
gboolean installer_system_support_efi ()
{
    return g_file_test("/sys/firmware/efi", G_FILE_TEST_IS_DIR);
}

JS_EXPORT_API 
gboolean installer_disk_is_gpt(const char* disk)
{
    PedDisk* peddisk = (PedDisk *) g_hash_table_lookup(disks, disk);
    if (peddisk != NULL) {
        if ((peddisk->type != NULL) && (g_strcmp0 ("gpt", peddisk->type->name) == 0)) {
            return TRUE;
        }
    } else {
        g_warning ("get disk sector size:find peddisk by %s failed\n", disk);
    }
    return FALSE;
}

JS_EXPORT_API
gchar* installer_get_partition_type (const gchar *part)
{
    gchar *type = NULL;
    PedPartition *pedpartition = NULL;
    if (part == NULL) {
        g_warning ("get partition type:part NULL\n");
        return type;
    }

    pedpartition = (PedPartition *) g_hash_table_lookup (partitions, part);

    if (pedpartition != NULL) {
        PedPartitionType part_type = pedpartition->type;
        switch (part_type) {
            case PED_PARTITION_NORMAL:
                type = g_strdup ("normal");
                break;
            case PED_PARTITION_LOGICAL:
                type = g_strdup ("logical");
                break;
            case PED_PARTITION_EXTENDED:
                type = g_strdup ("extended");
                break;
            case PED_PARTITION_FREESPACE:
                type = g_strdup ("freespace");
                break;
            case PED_PARTITION_METADATA:
                type = g_strdup ("metadata");
                break;
            case PED_PARTITION_PROTECTED:
                type = g_strdup ("protected");
                break;
            default:
                if (part_type > PED_PARTITION_PROTECTED) {
                    type = g_strdup ("protected");
                } else if (part_type > PED_PARTITION_METADATA) {
                    type = g_strdup ("metadata");
                } else if (part_type > PED_PARTITION_FREESPACE) {
                    type = g_strdup ("freespace");
                } else {
                    g_warning ("invalid type:%d\n", part_type);
                    type = g_strdup ("protected");
                }
                break;
        }
    } else {
        g_warning ("get partition type:find pedpartition %s failed\n", part);
    }

    return type;
}

JS_EXPORT_API
gchar *installer_get_partition_name (const gchar *part)
{
    gchar *name = NULL;
    PedPartition *pedpartition = NULL;
    if (part == NULL) {
        g_warning ("get partition name:part NULL\n");
        return name;
    }

    pedpartition = (PedPartition *) g_hash_table_lookup (partitions, part);
    if (pedpartition != NULL) {
        if (ped_disk_type_check_feature (pedpartition->disk->type, PED_DISK_TYPE_PARTITION_NAME)) {
            name = g_strdup (ped_partition_get_name (pedpartition));
        }
    } else {
        g_warning ("get partition name:find pedpartition %s failed\n", part);
    }

    return name;
}

JS_EXPORT_API
gchar* installer_get_partition_path (const gchar *uuid)
{
    PedPartition *part = (PedPartition *) g_hash_table_lookup (partitions, uuid);
    g_assert(part != NULL);

    if (part->num == -1) {
	char* ret = g_strdup_printf("free:%s:%d", part->disk->dev->path, (int)part->geom.start);
        g_warning ("installer_get_partition_path receive an free_path: %s\n", ret);
	return ret;
    }

    gchar *path = ped_partition_get_path (part);
    g_assert(path != NULL);
    return path;
}

JS_EXPORT_API 
gchar* installer_get_partition_mp (const gchar *uuid)
{
    g_return_val_if_fail(uuid != NULL, NULL);
    PedPartition* part = (PedPartition *) g_hash_table_lookup (partitions, uuid);
    g_return_val_if_fail(part != NULL, NULL);
    g_return_val_if_fail(part->num != -1, NULL);

    gchar* path = ped_partition_get_path (part);
    gchar* mp = get_partition_mount_point (path);
    g_free (path);

    return mp;
}

JS_EXPORT_API
double installer_get_partition_start (const gchar *uuid)
{
    if (uuid == NULL) {
        g_warning ("get partition start:part NULL\n");
        return 0;
    }

    PedPartition* part = (PedPartition *) g_hash_table_lookup (partitions, uuid);
    if (part != NULL) {
        return part->geom.start * part->disk->dev->sector_size;

    } else {
        g_warning ("get partition start:find pedpartition %s failed\n", uuid);
    }

    return 0;
}

JS_EXPORT_API
double installer_get_partition_size (const gchar *uuid)
{
    if (uuid == NULL) {
        g_warning ("get partition length:part NULL\n");
        return 0;
    }

    PedPartition* part = (PedPartition *) g_hash_table_lookup (partitions, uuid);
    if (part != NULL) {
        return part->geom.length * part->disk->dev->sector_size;

    } else {
        g_warning ("get partition length:find pedpartition %s failed\n", uuid);
    }

    return 0;
}

JS_EXPORT_API
double installer_get_partition_end (const gchar *uuid)
{
    if (uuid == NULL) {
        g_warning ("get partition end:part NULL\n");
        return 0;
    }

    PedPartition* part = (PedPartition *) g_hash_table_lookup (partitions, uuid);
    if (part != NULL) {
        return part->geom.end * part->disk->dev->sector_size;
    } else {
        g_warning ("get partition end:find pedpartition %s failed\n", uuid);
    }

    return 0;
}

JS_EXPORT_API
gchar *installer_get_partition_fs (const gchar *part)
{
    gchar *fs = NULL;
    PedPartition *pedpartition = NULL;
    if (part == NULL) {
        g_warning ("get partition fs:part NULL\n");
        return fs;
    }

    pedpartition = (PedPartition *) g_hash_table_lookup (partitions, part);
    if (pedpartition != NULL) {
        PedGeometry *geom = ped_geometry_duplicate (&pedpartition->geom);
        PedFileSystemType *fs_type = ped_file_system_probe (geom);
        if (fs_type != NULL) {
            fs = g_strdup (fs_type->name);
        }
        ped_geometry_destroy (geom);

    } else {
        g_warning ("get partition fs:find pedpartition %s failed\n", part);
    }
    if (fs != NULL) {
        g_debug ("get partition fs:fs is %s\n", fs);
        if (g_strrstr (fs, "swap") != NULL) {
            return g_strdup ("swap");
        }
    } 

    return fs;
}

JS_EXPORT_API 
gchar* installer_get_partition_label (const gchar *part)
{
    gchar *label = NULL;
    PedPartition *pedpartition = NULL;
    gchar *path = NULL;
    if (part == NULL) {
        g_warning ("get partition label:part NULL\n");
        return NULL;
    }

    pedpartition = (PedPartition *) g_hash_table_lookup (partitions, part);
    if (pedpartition == NULL) {
        g_warning ("get partition label:find pedpartition %s failed\n", part);
        return NULL;
    }
    path = ped_partition_get_path (pedpartition);
    if (path == NULL) {
        g_warning ("get partition label:get part %s path failed\n", part);
        return NULL;
    }
    label = get_partition_label (path);
    if (label != NULL) {
        label = g_strstrip (label);
    }
    g_free (path);

    return label;
}

JS_EXPORT_API 
gboolean installer_is_partition_busy (const gchar *uuid)
{
    g_return_val_if_fail(uuid != NULL, FALSE);
    PedPartition* part = (PedPartition *) g_hash_table_lookup (partitions, uuid);
    g_return_val_if_fail(part != NULL, FALSE);

    return ped_partition_is_busy(part);
}

JS_EXPORT_API 
gboolean installer_get_partition_flag (const gchar *part, const gchar *flag_name)
{
    gboolean result = FALSE;
    PedPartition *pedpartition = NULL;
    PedPartitionFlag flag;
    if (part == NULL || flag_name == NULL) {
        g_warning ("get partition flag:part or flag name NULL\n");
        return result;
    }

    pedpartition = (PedPartition *) g_hash_table_lookup (partitions, part);
    if (pedpartition == NULL) {
        g_warning ("get partition flag:find pedpartition %s failed\n", part);
        goto out;
    }
    if (!ped_partition_is_active (pedpartition)) {
        g_warning ("get partition flag: ped partition flag not active\n");
        goto out;
    }
    flag = ped_partition_flag_get_by_name (flag_name);
    if (flag == 0) {
        g_warning ("get partition flag: ped partition flag get by name failed\n");
        goto out;
    }
    if (ped_partition_is_flag_available (pedpartition, flag)) {
        result = (gboolean) ped_partition_get_flag (pedpartition, flag);
    }
    goto out;

out:
    return result;
}

JS_EXPORT_API 
void installer_get_partition_free (const gchar *part)
{
    PedPartition *pedpartition = NULL;
    if (part == NULL) {
        g_warning ("get partition free:part NULL\n");
        return;
    }

    pedpartition = (PedPartition *) g_hash_table_lookup (partitions, part);
    if (pedpartition != NULL) {
        PedPartitionType part_type = pedpartition->type;

        if (part_type != PED_PARTITION_NORMAL && part_type != PED_PARTITION_LOGICAL && part_type != PED_PARTITION_EXTENDED) {
            g_printf ("get partition free:no meaning for none used\n");
            return;
        }

        const gchar *fs = NULL;
        PedGeometry *geom = ped_geometry_duplicate (&pedpartition->geom);
        PedFileSystemType *fs_type = ped_file_system_probe (geom);
        if (fs_type != NULL) {
            fs = fs_type->name;
        }
	ped_geometry_destroy (geom);
	if (g_strstr_len(fs, -1, "swap")) {
	    JSObjectRef message = json_create ();
	    json_append_string (message, "part", part);
	    json_append_number (message, "free", installer_get_partition_size (part));
	    js_post_message ("used", message);
	    return;
	}

        if (fs == NULL) {
            g_warning ("get partition free:get partition file system failed\n");
            return;
        }

        gchar *path = ped_partition_get_path (pedpartition);
        if (path != NULL) {
            struct FsHandler *handler = g_new0 (struct FsHandler, 1);
            handler->path = g_strdup (path);
            handler->part = g_strdup (part);
            handler->fs = g_strdup (fs);
            GThread *thread = g_thread_new ("get_partition_free", 
                                            (GThreadFunc) get_partition_free, 
                                            (gpointer) handler);
            g_thread_unref (thread);
        } else {
            g_warning ("get pedpartition free: get %s path failed\n", part);
        }
        g_free (path);

    } else {
        g_warning ("get partition free:find pedpartition %s failed\n", part);
    }
}

JS_EXPORT_API 
gchar* installer_get_partition_os (const gchar *part)
{
    gchar* result = NULL;
    gchar *path = NULL;
    PedPartition *pedpartition = NULL;
    if (part == NULL) {
        g_warning ("get partition os:part NULL\n");
        return result;
    }

    pedpartition = (PedPartition *) g_hash_table_lookup (partitions, part);
    if (pedpartition != NULL) {

        path = ped_partition_get_path (pedpartition);
        if (path != NULL) {
            result = g_strdup (g_hash_table_lookup (partition_os, path));

        } else {
            g_warning ("get pedpartition os: get %s path failed\n", part);
        }
        g_free (path);

    } else {
        g_warning ("get partition os:find pedpartition %s failed\n", part);
    }

    return result;
}

JS_EXPORT_API 
gchar* installer_get_partition_os_desc (const gchar *part)
{
    gchar* result = NULL;
    gchar *path = NULL;
    PedPartition *pedpartition = NULL;
    if (part == NULL) {
        g_warning ("get partition os desc:part NULL\n");
        return result;
    }

    pedpartition = (PedPartition *) g_hash_table_lookup (partitions, part);
    if (pedpartition != NULL) {

        path = ped_partition_get_path (pedpartition);
        if (path != NULL) {
            result = g_strdup (g_hash_table_lookup (partition_os_desc, path));

        } else {
            g_warning ("get pedpartition os desc: get %s path failed\n", part);
        }
        g_free (path);

    } else {
        g_warning ("get partition os desc:find pedpartition %s failed\n", part);
    }

    return result;
}

JS_EXPORT_API 
gboolean installer_new_disk_partition (const gchar *part_uuid, const gchar *disk_uuid, const gchar *type, const gchar *fs, double byte_start, double byte_end)
{
    g_assert(part_uuid != NULL);
    g_assert(disk_uuid != NULL);
    g_assert(type != NULL);
    g_assert(byte_start >= 0);
    g_assert(byte_end > 0);

    PedPartitionType part_type;
    if (g_strcmp0 (type, "normal") == 0) {
	part_type = PED_PARTITION_NORMAL;
    } else if (g_strcmp0 (type, "logical") == 0) {
	part_type = PED_PARTITION_LOGICAL;
    } else if (g_strcmp0 (type, "extended") == 0) {
	part_type = PED_PARTITION_EXTENDED;
    } else {
	g_assert_not_reached();
    }

    const PedFileSystemType *part_fs = NULL;
    if (part_type != PED_PARTITION_EXTENDED) {
	part_fs = ped_file_system_type_get (fs);
	if (part_fs == NULL) {
	    g_warning("new disk partition:ped file system type get for %s is NULL\n", fs);
	}
    }

    PedDisk* disk = (PedDisk *) g_hash_table_lookup (disks, disk_uuid);
    g_assert(disk != NULL);
    PedSector start = (PedSector) ceil(byte_start / disk->dev->sector_size);
    PedSector end = (PedSector) floor(byte_end / disk->dev->sector_size);
    PedPartition* part = create_and_add_partition(disk, part_type, part_fs, start, end);
    g_return_val_if_fail(part != NULL, FALSE);

    g_hash_table_insert (partitions,  g_strdup (part_uuid), part);
    return TRUE;
}

JS_EXPORT_API 
gboolean installer_delete_disk_partition (const gchar *part_uuid)
{
    g_assert(part_uuid != NULL);

    PedPartition* part = (PedPartition *) g_hash_table_lookup (partitions, part_uuid);
    g_assert(part != NULL);
    g_message("-----------delete %s:(%s)-----------\n", part->disk->dev->path, part_uuid);
    
    if ((ped_disk_delete_partition (part->disk, part) != 0)) {
	g_message("delete disk partition:ok\n");
	return TRUE;
    } else {
	g_warning ("delete disk partition:failed\n");
	return FALSE;
    }
}

JS_EXPORT_API 
gboolean installer_update_partition_geometry (const gchar *uuid, double byte_start, double byte_size) 
{
    g_return_val_if_fail(uuid != NULL, FALSE);
    g_return_val_if_fail(byte_start >= 0, FALSE);
    g_return_val_if_fail(byte_size > 0, FALSE);


    PedGeometry *geom = NULL;

    PedPartition *part =  (PedPartition *) g_hash_table_lookup (partitions, uuid);
    g_assert(part != NULL);

    geom = &part->geom;
    g_assert(geom != NULL);

    PedSector start = (PedSector) ceil(byte_start / part->disk->dev->sector_size);
    PedSector length = (PedSector) floor(byte_size / part->disk->dev->sector_size);
    g_message("-----------update part->%s geometry start->%d length->%d\n------------", uuid, (int)start, (int)length);
    ped_geometry_set (geom,  start, length);
    return TRUE;
}

JS_EXPORT_API
gboolean installer_update_partition_fs (const gchar *uuid, const gchar *fs)
{
    g_return_val_if_fail(uuid != NULL, FALSE);
    g_return_val_if_fail(fs != NULL, FALSE);

    PedPartition *part = (PedPartition *) g_hash_table_lookup (partitions, uuid);
    g_return_val_if_fail(part != NULL, FALSE);

    const PedFileSystemType *part_fs_type = NULL;
    if (g_strcmp0 (fs, "efi") == 0) {
	part_fs_type = ped_file_system_type_get ("fat32");
    } else if (g_strcmp0 (fs, "swap") == 0) {
	part_fs_type = ped_file_system_type_get ("linux-swap");
    } else {
	part_fs_type = ped_file_system_type_get (fs);
    }

    if (part_fs_type == NULL) {
	g_warning ("update partition fs:get part fs type %s failed\n", fs);
	return FALSE;
    }

    if ((ped_partition_set_system (part, part_fs_type)) == 0) {
	g_warning ("update partition fs: ped partition set system %s failed\n", fs);
	return FALSE;
    }

    const gchar *part_path = ped_partition_get_path (part);
    g_assert(part_path != NULL);

    //TODO: create EFI directory
    if (g_strcmp0 (fs, "efi") == 0) {
	mkfs_latter(part_path, "fat32");
	if (! installer_set_partition_flag (uuid, "boot", 1)) {
	    g_warning ("set flag for uefi failed\n");
	}
    } else {
	mkfs_latter(part_path, fs);
    }
    return TRUE;
}

JS_EXPORT_API 
gboolean installer_write_disk (const gchar *uuid)
{
    g_assert(uuid != NULL);

    PedDisk *disk = (PedDisk *) g_hash_table_lookup (disks, uuid);
    if (disk != NULL) {
	g_message ("write disk->%s\n", disk->dev->path);
        if ((ped_disk_commit_to_dev (disk)) == 0) {
            g_warning ("write disk(%s):commit to dev failed\n", disk->dev->path);
	    return FALSE;
        }
        if ((ped_disk_commit_to_os (disk)) == 0) {
            g_warning ("write disk(%s):commit to os failed\n", disk->dev->path);
            return FALSE;
        }
        g_spawn_command_line_async ("sync", NULL);
	return TRUE;
    } else {
        g_warning ("write disk:find peddisk %s failed\n", uuid);
	return FALSE;
    }
}

JS_EXPORT_API 
gboolean installer_set_partition_flag (const gchar *uuid, const gchar *flag_name, gboolean status)
{
    g_return_val_if_fail(uuid!= NULL, FALSE);
    g_return_val_if_fail(flag_name != NULL, FALSE);

    PedPartition* part = (PedPartition *) g_hash_table_lookup (partitions, uuid);
    g_return_val_if_fail(part != NULL, FALSE);

    PedPartitionFlag flag = ped_partition_flag_get_by_name (flag_name);
    if (ped_partition_is_flag_available (part, flag)) {
	ped_partition_set_flag (part, flag, status);
	return TRUE;
    } else {
	g_warning ("set partition flag:flag %s is not available\n", flag_name);
	return FALSE;
    }
}


char* find_partition_path_by_sector_and_disk_path(const char* path, int start)
{
    PedDevice* dev = ped_device_get(path);
    g_return_val_if_fail(dev != NULL, NULL);
    PedDisk* disk = ped_disk_new(dev);
    g_return_val_if_fail(disk != NULL, NULL);

    PedPartition* part = ped_disk_get_partition_by_sector(disk, (PedSector)start);
    g_return_val_if_fail(part != NULL, NULL);
    g_return_val_if_fail(part->num != -1, NULL);

    return g_strdup(ped_partition_get_path(part));
}


//when dectect mount partition, tell user to unmount them
JS_EXPORT_API
void installer_unmount_partition (const gchar *part)
{
    if (part == NULL) {
        g_warning ("unmount partition:part NULL\n");
        return;
    }
    gchar *path = installer_get_partition_path (part);
    unmount_partition_by_device (path);
    g_free (path);
}


void partition_print(char* uuid, PedPartition* part)
{
    printf("Partition: %s ==== %s(%d)\n", uuid, ped_partition_get_path(part), part->num);
}
void disk_print(char* uuid, PedDisk* disk)
{
    printf("Disk: %s ==== %s\n", uuid, disk->dev->path);
}
void ped_print()
{
    g_hash_table_foreach(disks, (GHFunc)disk_print, NULL);
    g_hash_table_foreach(partitions, (GHFunc)partition_print, NULL);
}
