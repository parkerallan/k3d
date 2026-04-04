
SRC_DIR = example
BUILD_DIR = build
TARGET = $(BUILD_DIR)/test.elf
ROMDISK_IMG = $(BUILD_DIR)/romdisk.img
ROMDISK_OBJ = $(BUILD_DIR)/romdisk.o
OBJS = $(BUILD_DIR)/test.o $(BUILD_DIR)/pvr-texture.o \
	$(BUILD_DIR)/k3d_loader.o $(BUILD_DIR)/k3d_animation.o \
	$(BUILD_DIR)/font.o $(ROMDISK_OBJ)
KOS_ROMDISK_DIR = romdisk
CPPFLAGS += -I$(SRC_DIR)

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	kos-cc $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cc | $(BUILD_DIR)
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	kos-c++ $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.m | $(BUILD_DIR)
	kos-cc $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.mm | $(BUILD_DIR)
	kos-c++ $(CFLAGS) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	kos-as $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	kos-cc -c $< -o $@

$(ROMDISK_IMG): | $(BUILD_DIR)
	$(KOS_GENROMFS) -f $(ROMDISK_IMG) -d $(KOS_ROMDISK_DIR) -v -x .gitignore -x .DS_Store -x Thumbs.db

$(ROMDISK_OBJ): $(ROMDISK_IMG) | $(BUILD_DIR)
	$(KOS_BASE)/utils/bin2c/bin2c $(ROMDISK_IMG) $(BUILD_DIR)/romdisk_tmp.c romdisk
	kos-cc -o $(BUILD_DIR)/romdisk_tmp.o -c $(BUILD_DIR)/romdisk_tmp.c
	$(KOS_CC) -o $(ROMDISK_OBJ) -r $(BUILD_DIR)/romdisk_tmp.o $(KOS_LIB_PATHS) -Wl,--whole-archive -lromdiskbase
	rm -f $(BUILD_DIR)/romdisk_tmp.c $(BUILD_DIR)/romdisk_tmp.o

clean: rm-elf
	-rm -rf $(BUILD_DIR)

rm-elf:
	-rm -f $(TARGET) $(OBJS) $(ROMDISK_IMG) $(BUILD_DIR)/romdisk_tmp.c $(BUILD_DIR)/romdisk_tmp.o

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS) -L$(KOS_BASE)/lib -lKGL

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS) $(ROMDISK_IMG)
	$(KOS_STRIP) $(TARGET)

