CC = gcc
CFLAGS = -Wall -O2
LIBS = -lfuse3

TARGET = nxfs
IMG = nxfs.img
MNT = mnt

all:
	$(CC) nxfs.c -o $(TARGET) $(CFLAGS) $(LIBS)

# ================= FS 생성 =================
mkfs: all
	./$(TARGET) mkfs $(IMG)

# ================= 마운트 =================
m: all
	mkdir -p $(MNT)
	./$(TARGET) mount $(IMG) $(MNT)

# ================= 언마운트 =================
u:
	fusermount3 -u $(MNT)

# ================= 테스트 =================
test:
	echo hello > $(MNT)/a.txt
	cat $(MNT)/a.txt
	ls $(MNT)

# ================= 정리 =================
clean:
	rm -f $(TARGET) $(IMG)