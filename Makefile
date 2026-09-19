CXX      = g++
CXXFLAGS = -std=c++23 -O2 -Wall -Wno-unused-result -I package -I package/modules
LDFLAGS  = -lSDL2 -lSDL2_ttf -lSDL2_image -lGL -lGLU -lm

TARGET = space_game_v2
BUILD  = build

# Every .cpp under engine/ and modules/ is compiled in - that is what "drop-in module" means.
# A folder/file whose name starts with '_' is skipped (handy for disabling a module).
SRCS = $(shell find package/engine package/modules -name '*.cpp' -not -path '*/_*')
OBJS = $(SRCS:%.cpp=$(BUILD)/%.o)

.PHONY: all clean run modules
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET)

modules: $(TARGET)
	./$(TARGET) --list-modules

clean:
	rm -rf $(BUILD) $(TARGET)

-include $(OBJS:.o=.d)
