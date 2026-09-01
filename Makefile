CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/core
BUILD    = build
CORE     = firmware/core/mosaik_proto.c firmware/core/mosaik_node.c

.PHONY: test clean

test: $(BUILD)/test_mosaik
	./$(BUILD)/test_mosaik

$(BUILD)/test_mosaik: $(CORE) test/test_mosaik.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CORE) test/test_mosaik.c -o $@

clean:
	rm -rf $(BUILD)
