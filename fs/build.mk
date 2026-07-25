# =========================
# Filesystem sources
# =========================

FS_C_SRCS := \
	fs/fat32_core.c \
	fs/fat32_name.c \
	fs/fat32_dir.c \
	fs/fat32_file.c \
	fs/fat32.c \
	fs/nxfs.c \
	fs/nxfs_io.c \
	fs/vfs.c \
	fs/vfs_path.c \
	fs/vfs_mount.c \
	fs/vfs_devfs.c \
	fs/vfs_procfs.c \
	fs/vfs_procfs_format.c \
	fs/vfs_eventfs.c \
	fs/vfs_eventfs_format.c \
	fs/vfs_proc_actions.c \
	fs/vfs_io.c

I386_FS_C_SRCS := \
	fs/fat32_core.c \
	fs/fat32_name.c \
	fs/fat32_dir.c \
	fs/fat32_file.c \
	fs/fat32.c \
	fs/nxfs.c \
	fs/nxfs_io.c \
	fs/early_vfs.c \
	fs/vfs.c \
	fs/vfs_mount.c \
	fs/vfs_path.c \
	fs/vfs_io.c \
	fs/vfs_devfs.c \
	fs/vfs_procfs.c \
	fs/vfs_procfs_format.c \
	fs/vfs_eventfs.c \
	fs/vfs_eventfs_format.c \
	fs/vfs_proc_actions.c
