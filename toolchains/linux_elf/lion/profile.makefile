BUILD=./build

TARGET=lion-pie
SRC=lion.profile.c
OBJS=$(SRC:.c=.o)
CFLAGS=-Wall -fPIC
LDFLAGS=-pie

all: $(BUILD) $(BUILD)/$(TARGET) 

.PHONY: clean all
clean:
	rm -r $(BUILD)

$(BUILD)/$(TARGET): $(addprefix $(BUILD)/,$(OBJS))
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

$(BUILD):
	mkdir -p $(BUILD)

