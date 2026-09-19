CXX      = g++
CXXFLAGS = -std=c++23 -O2 -Wall -Wno-unused-result -I package -I package/modules
LDFLAGS  = -lSDL2 -lSDL2_ttf -lSDL2_image -lSDL2_mixer -lGL -lGLU -lm

TARGET = space_game_v2
BUILD  = build

# Every .cpp under engine/ and modules/ is compiled in - that is what "drop-in module" means.
# A folder/file whose name starts with '_' is skipped (handy for disabling a module).
SRCS = $(shell find package/engine package/modules -name '*.cpp' -not -path '*/_*')
OBJS = $(SRCS:%.cpp=$(BUILD)/%.o)

LIST = $(BUILD)/sources.list

.PHONY: all clean run modules test test-san FORCE
all: $(TARGET)

# Rewritten only when a module is added/removed/renamed, so the binary relinks exactly then.
$(LIST): FORCE
	@mkdir -p $(BUILD)
	@echo "$(SRCS)" | tr ' ' '\n' | sort > $(LIST).tmp
	@cmp -s $(LIST).tmp $(LIST) && rm $(LIST).tmp || mv $(LIST).tmp $(LIST)

$(TARGET): $(OBJS) $(LIST)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET)

modules: $(TARGET)
	./$(TARGET) --list-modules

# Unit tests: engine + input handler logic + package/tests, no SDL/GL needed.
TEST_SRCS = $(shell find package/tests -name '*.cpp') $(filter-out package/engine/main.cpp,$(wildcard package/engine/*.cpp)) package/modules/core/input_handler/input_handler.cpp package/modules/core/settings/settings.cpp package/modules/core/save_system/save_system.cpp
TEST_OBJS = $(TEST_SRCS:%.cpp=$(BUILD)/%.o)

$(BUILD)/test_runner: $(TEST_OBJS)
	$(CXX) -o $@ $(TEST_OBJS)

test: $(BUILD)/test_runner
	./$(BUILD)/test_runner

# Same tests under AddressSanitizer + UBSan (catches use-after-free, overflows, UB the plain run can miss).
test-san:
	$(MAKE) BUILD=build_san CXX="g++ -fsanitize=address,undefined -fno-omit-frame-pointer -g" CXXFLAGS="-std=c++23 -O1 -I package -I package/modules" test

clean:
	rm -rf $(BUILD) build_san $(TARGET)

-include $(OBJS:.o=.d)
