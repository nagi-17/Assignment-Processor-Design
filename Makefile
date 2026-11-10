# ==============================================================================
# Makefile for RISC201 Processor Simulation Environment
# Computer Organization & Architecture (CSC-201) Assignment
#
# Unified master Makefile managing:
#   1. simulator               - Main 4-stage & 6-stage CLI and TUI simulator
#   2. assembler               - RISC201 two-pass assembler
#   3. disassembler           - Binary/Hex to assembly disassembler
#   4. microcontrol_simulator  - Standalone Microprogrammed Control Unit model
#   5. test                    - Comprehensive test suites for all subsystems
# ==============================================================================

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -O2

# Include search paths covering all subsystems
INCLUDES = -I src/headers \
           -I src/headers/latches \
           -I src/debugger \
           -I src/simulator/pipeline/memory \
           -I src/simulator/pipeline/alu \
           -I src/simulator/pipeline/fpu \
           -I src/simulator/pipeline/execute \
           -I src/simulator \
           -I src/microcontrol \
           -I src/assembler \
           -I src/disassembler \
           -I src/tui

# ------------------------------------------------------------------------------
# 1. Main Simulator & Pipeline Components
# ------------------------------------------------------------------------------
COMMON_SRCS = src/simulator/pipeline/instruction_fetch/instruction_fetch.cpp \
              src/simulator/pipeline/instruction_decode/instruction_decode.cpp \
              src/simulator/pipeline/control_status_unit/control_status_unit.cpp \
              src/simulator/pipeline/alu/alu_add_sub.cpp \
              src/simulator/pipeline/alu/alu_bitwise.cpp \
              src/simulator/pipeline/alu/alu_mul_div.cpp \
              src/simulator/pipeline/fpu/fpu_helpers.cpp \
              src/simulator/pipeline/fpu/fpu_addsub.cpp \
              src/simulator/pipeline/fpu/fpu_muldiv.cpp \
              src/simulator/pipeline/fpu/fpu_cmp.cpp \
              src/debugger/debugger.cpp \
              src/debugger/debugger_cli.cpp

COMMON_OBJS = $(COMMON_SRCS:.cpp=.o)

MICRO_SRCS = src/microcontrol/microprogram_store.cpp \
             src/microcontrol/microprogrammed_cpu.cpp \
             src/microcontrol/micro_session.cpp

MICRO_OBJS = $(MICRO_SRCS:.cpp=.o)

SESSION_SRCS = src/simulator/four_stage_session.cpp \
               src/simulator/six_stage_session.cpp

SESSION_OBJS = $(SESSION_SRCS:.cpp=.o)

# ------------------------------------------------------------------------------
# 2. TUI Full-Screen Debugger Components
# ------------------------------------------------------------------------------
TUI_SRCS = src/tui/app.cpp \
           src/tui/debugger_model.cpp \
           src/tui/instruction_text.cpp \
           src/tui/renderer.cpp \
           src/tui/screen.cpp \
           src/tui/terminal.cpp

TUI_OBJS = $(TUI_SRCS:.cpp=.o)

# ------------------------------------------------------------------------------
# 3. Assembler & Disassembler Components
# ------------------------------------------------------------------------------
ASSEMBLER_SRCS = src/assembler/main_assembler.cpp \
                 src/assembler/preprocessor.cpp \
                 src/assembler/symbol_table.cpp \
                 src/assembler/bin_generator.cpp

DISASSEMBLER_SRCS = src/disassembler/main_disassembler.cpp \
                    src/disassembler/disassembler.cpp

# ------------------------------------------------------------------------------
# 4. Standalone Microcontrol Model Components
# ------------------------------------------------------------------------------
MC_SRCS = src/microcontrol/microprogram_store.cpp \
          src/microcontrol/microprogrammed_cpu.cpp \
          src/simulator/pipeline/alu/alu_add_sub.cpp \
          src/simulator/pipeline/alu/alu_bitwise.cpp \
          src/simulator/pipeline/alu/alu_mul_div.cpp \
          src/simulator/pipeline/fpu/fpu_helpers.cpp \
          src/simulator/pipeline/fpu/fpu_addsub.cpp \
          src/simulator/pipeline/fpu/fpu_muldiv.cpp \
          src/simulator/pipeline/fpu/fpu_cmp.cpp \
          src/simulator/pipeline/control_status_unit/control_status_unit.cpp

MC_OBJS = $(MC_SRCS:.cpp=.mc.o)

# ------------------------------------------------------------------------------
# Primary Build Targets
# ------------------------------------------------------------------------------
all: simulator assembler disassembler microcontrol_simulator

# Main Simulator executable (incorporating CLI, Debugger, TUI, and Microcontrol Session)
simulator: $(COMMON_OBJS) $(SESSION_OBJS) $(MICRO_OBJS) $(TUI_OBJS) \
           src/tui/four_stage_entry.o src/tui/six_stage_entry.o \
           src/simulator/four_stage_runner.o src/simulator/six_stage_runner.o src/simulator/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# Assembler executable
assembler: $(ASSEMBLER_SRCS)
	$(CXX) $(CXXFLAGS) -I src/headers src/assembler/main_assembler.cpp -o $@

# Disassembler executable
disassembler: $(DISASSEMBLER_SRCS)
	$(CXX) $(CXXFLAGS) -I src/headers -I src/disassembler $^ -o $@

# Standalone Microcontrol Simulator executable
microcontrol: microcontrol_simulator
microcontrol_simulator: $(MC_OBJS) src/microcontrol/microcontrol_main.mc.o
	$(CXX) $(CXXFLAGS) $^ -o $@

microcontrol_tests: $(MC_OBJS) tests/microcontrol/test_microcontrol.mc.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# Debugger Standalone Mock Demo
debugger_demo: examples/debugger_mock.cpp src/debugger/debugger.cpp src/debugger/debugger_cli.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

# ------------------------------------------------------------------------------
# Object File Compilation Rules
# ------------------------------------------------------------------------------
src/simulator/four_stage_runner.o: src/simulator/four_stage_runner.cpp src/simulator/four_stage_cpu.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DFOUR_STAGE_PIPELINE -c src/simulator/four_stage_runner.cpp -o $@

src/simulator/six_stage_runner.o: src/simulator/six_stage_runner.cpp src/simulator/six_stage_cpu.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c src/simulator/six_stage_runner.cpp -o $@

src/tui/four_stage_entry.o: src/tui/four_stage_entry.cpp src/simulator/four_stage_cpu.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DFOUR_STAGE_PIPELINE -c src/tui/four_stage_entry.cpp -o $@

src/tui/six_stage_entry.o: src/tui/six_stage_entry.cpp src/simulator/six_stage_cpu.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c src/tui/six_stage_entry.cpp -o $@

src/simulator/four_stage_session.o: src/simulator/four_stage_session.cpp \
        src/simulator/four_stage_cpu.hpp src/simulator/engine_session.hpp \
        src/simulator/control_rom_view.hpp src/simulator/control_rom_session_impl.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DFOUR_STAGE_PIPELINE -c src/simulator/four_stage_session.cpp -o $@

src/simulator/six_stage_session.o: src/simulator/six_stage_session.cpp \
        src/simulator/six_stage_cpu.hpp src/simulator/engine_session.hpp \
        src/simulator/control_rom_view.hpp src/simulator/control_rom_session_impl.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c src/simulator/six_stage_session.cpp -o $@

src/microcontrol/micro_session.o: src/microcontrol/micro_session.cpp \
        src/microcontrol/microprogrammed_cpu.hpp src/simulator/engine_session.hpp \
        src/simulator/control_rom_view.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c src/microcontrol/micro_session.cpp -o $@

src/simulator/main.o: src/simulator/main.cpp src/simulator/engine_session.hpp $(ASSEMBLER_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c src/simulator/main.cpp -o $@

%.mc.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ------------------------------------------------------------------------------
# Testing Targets
# ------------------------------------------------------------------------------
test: test-debugger test-memory test-pipeline test-disassembler test-microcontrol test-tui
	@echo ""
	@echo "======================================================="
	@echo "   ALL TEST SUITES (PIPELINE, MEMORY, DEBUGGER,"
	@echo "   DISASSEMBLER, MICROCONTROL, TUI) PASSED!            "
	@echo "======================================================="

test-debugger:
	@echo "=== [1/6] Running Debugger Unit & Adapter Tests ==="
	@$(CXX) $(CXXFLAGS) -I src/debugger -I src/headers tests/debugger_tests.cpp src/debugger/debugger.cpp -o /tmp/t_dbg && /tmp/t_dbg && rm /tmp/t_dbg
	@$(CXX) $(CXXFLAGS) -I src/debugger -I src/headers -I src/headers/latches tests/debugger_callback_target_test.cpp -o /tmp/t_cb && /tmp/t_cb && rm /tmp/t_cb
	@$(CXX) $(CXXFLAGS) -I src/debugger -I src/headers -I src/headers/latches tests/debugger_four_stage_adapter_test.cpp src/debugger/debugger.cpp -o /tmp/t_ad4 && /tmp/t_ad4 && rm /tmp/t_ad4
	@$(CXX) $(CXXFLAGS) -I src/debugger -I src/headers -I src/headers/latches tests/debugger_six_stage_adapter_test.cpp src/debugger/debugger.cpp -o /tmp/t_ad6 && /tmp/t_ad6 && rm /tmp/t_ad6

test-memory:
	@echo "=== [2/6] Running Memory Subsystem Tests ==="
	@$(CXX) $(CXXFLAGS) -I src/simulator/pipeline/memory src/simulator/pipeline/memory/test_memorysystem.cpp -o /tmp/t_mem && /tmp/t_mem && rm /tmp/t_mem

test-pipeline: simulator
	@echo "=== [3/6] Running Pipeline Multi-Cycle & Hazard Tests ==="
	@$(CXX) $(CXXFLAGS) -I src/headers -I src/headers/latches -I tests/pipeline_tests \
	    tests/pipeline_tests/test_main.cpp \
	    tests/pipeline_tests/test_instruction_fetch.cpp \
	    tests/pipeline_tests/test_instruction_decode.cpp \
	    tests/pipeline_tests/test_control_status_unit.cpp \
	    src/simulator/pipeline/instruction_fetch/instruction_fetch.cpp \
	    src/simulator/pipeline/instruction_decode/instruction_decode.cpp \
	    src/simulator/pipeline/control_status_unit/control_status_unit.cpp \
	    -o /tmp/t_pipe && /tmp/t_pipe && rm /tmp/t_pipe
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -I tests/pipeline_tests \
	    tests/pipeline_tests/test_execute_stage.cpp \
	    src/simulator/pipeline/execute/execute_stage.cpp \
	    src/simulator/pipeline/alu/alu_add_sub.cpp \
	    src/simulator/pipeline/alu/alu_bitwise.cpp \
	    src/simulator/pipeline/alu/alu_mul_div.cpp \
	    src/simulator/pipeline/fpu/fpu_helpers.cpp \
	    src/simulator/pipeline/fpu/fpu_addsub.cpp \
	    src/simulator/pipeline/fpu/fpu_muldiv.cpp \
	    src/simulator/pipeline/fpu/fpu_cmp.cpp \
	    src/simulator/pipeline/control_status_unit/control_status_unit.cpp \
	    -o /tmp/t_ex && /tmp/t_ex && rm /tmp/t_ex
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -I tests/pipeline_tests \
	    tests/pipeline_tests/test_multicycle_6stage.cpp $(COMMON_SRCS) \
	    -o /tmp/t_mc6 && /tmp/t_mc6 && rm /tmp/t_mc6
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -I tests/pipeline_tests -DFOUR_STAGE_PIPELINE \
	    tests/pipeline_tests/test_multicycle_4stage.cpp $(COMMON_SRCS) \
	    -o /tmp/t_mc4 && /tmp/t_mc4 && rm /tmp/t_mc4
	@./simulator tests/test_basic.s

test-disassembler: disassembler
	@echo "=== [4/6] Running Disassembler Roundtrip Tests ==="
	@mkdir -p build/disassembler
	@$(CXX) $(CXXFLAGS) -I src/headers -I src/disassembler tests/disassembler_tests.cpp src/disassembler/disassembler.cpp -o build/disassembler/disassembler_tests && ./build/disassembler/disassembler_tests

test-microcontrol: microcontrol_tests
	@echo "=== [5/6] Running Microcontrol Model Unit Tests ==="
	@./microcontrol_tests

test-tui:
	@echo "=== [6/6] Running TUI Renderer & Model Unit Tests ==="
	@$(CXX) $(CXXFLAGS) $(INCLUDES) tests/tui/test_tui.cpp src/tui/screen.cpp src/tui/renderer.cpp \
	    src/tui/instruction_text.cpp src/tui/debugger_model.cpp src/debugger/debugger.cpp -o /tmp/t_tui && /tmp/t_tui && rm /tmp/t_tui

# ------------------------------------------------------------------------------
# Cleanup Target
# ------------------------------------------------------------------------------
clean:
	rm -rf $(COMMON_OBJS) $(SESSION_OBJS) $(MICRO_OBJS) $(MC_OBJS) $(TUI_OBJS) \
	       src/tui/four_stage_entry.o src/tui/six_stage_entry.o \
	       src/simulator/four_stage_runner.o src/simulator/six_stage_runner.o src/simulator/main.o \
	       src/microcontrol/microcontrol_main.mc.o tests/microcontrol/test_microcontrol.mc.o \
	       simulator assembler disassembler microcontrol_simulator microcontrol_tests debugger_demo build/

.PHONY: all test test-debugger test-memory test-pipeline test-disassembler test-microcontrol test-tui \
        simulator assembler disassembler microcontrol microcontrol_simulator microcontrol_tests debugger_demo clean
