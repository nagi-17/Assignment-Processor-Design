# Makefile for RISC201 Processor Simulation Environment
# CSC-201 (COA) Assignment: Processor Design

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -O2

# Include search paths
INCLUDES = -I src/headers \
           -I src/headers/latches \
           -I src/debugger \
           -I src/simulator/pipeline/memory \
           -I src/simulator/pipeline/alu \
           -I src/simulator/pipeline/fpu \
           -I src/simulator/pipeline/execute \
           -I src/simulator \
           -I src/assembler

# Common pipeline & unit sources
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

all: simulator assembler

# Assembler build
ASSEMBLER_SRCS = src/assembler/main_assembler.cpp \
                 src/assembler/preprocessor.cpp \
                 src/assembler/symbol_table.cpp \
                 src/assembler/bin_generator.cpp

assembler: $(ASSEMBLER_SRCS)
	$(CXX) $(CXXFLAGS) -I src/headers src/assembler/main_assembler.cpp -o assembler

# Runner object files
src/simulator/four_stage_runner.o: src/simulator/four_stage_runner.cpp src/simulator/four_stage_cpu.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DFOUR_STAGE_PIPELINE -c src/simulator/four_stage_runner.cpp -o src/simulator/four_stage_runner.o

src/simulator/six_stage_runner.o: src/simulator/six_stage_runner.cpp src/simulator/six_stage_cpu.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c src/simulator/six_stage_runner.cpp -o src/simulator/six_stage_runner.o

src/simulator/main.o: src/simulator/main.cpp $(ASSEMBLER_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c src/simulator/main.cpp -o src/simulator/main.o

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Simulator executable
simulator: $(COMMON_OBJS) src/simulator/four_stage_runner.o src/simulator/six_stage_runner.o src/simulator/main.o
	$(CXX) $(CXXFLAGS) $^ -o simulator

# Run unit tests
test: simulator
	@echo "=== Running Debugger Helper Tests ==="
	@$(CXX) $(CXXFLAGS) -I src/debugger -I src/headers tests/debugger_tests.cpp src/debugger/debugger.cpp -o /tmp/t_dbg && /tmp/t_dbg && rm /tmp/t_dbg
	@echo "=== Running Memory Subsystem Tests ==="
	@$(CXX) $(CXXFLAGS) -I src/simulator/pipeline/memory src/simulator/pipeline/memory/test_memorysystem.cpp -o /tmp/t_mem && /tmp/t_mem && rm /tmp/t_mem
	@echo "=== Running Pipeline & Trap Unit Tests ==="
	@$(CXX) $(CXXFLAGS) -I src/headers -I src/headers/latches -I tests/pipeline_tests \
	    tests/pipeline_tests/test_main.cpp \
	    tests/pipeline_tests/test_instruction_fetch.cpp \
	    tests/pipeline_tests/test_instruction_decode.cpp \
	    tests/pipeline_tests/test_control_status_unit.cpp \
	    src/simulator/pipeline/instruction_fetch/instruction_fetch.cpp \
	    src/simulator/pipeline/instruction_decode/instruction_decode.cpp \
	    src/simulator/pipeline/control_status_unit/control_status_unit.cpp \
	    -o /tmp/t_pipe && /tmp/t_pipe && rm /tmp/t_pipe
	@echo "=== Running Execute Stage Multi-Cycle Unit Tests ==="
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
	@echo "=== Running Multi-Cycle Integration Tests (6-stage) ==="
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -I tests/pipeline_tests \
	    tests/pipeline_tests/test_multicycle_6stage.cpp $(COMMON_SRCS) \
	    -o /tmp/t_mc6 && /tmp/t_mc6 && rm /tmp/t_mc6
	@echo "=== Running Multi-Cycle Integration Tests (4-stage) ==="
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -I tests/pipeline_tests -DFOUR_STAGE_PIPELINE \
	    tests/pipeline_tests/test_multicycle_4stage.cpp $(COMMON_SRCS) \
	    -o /tmp/t_mc4 && /tmp/t_mc4 && rm /tmp/t_mc4
	@echo "=== Running Basic Pipeline Integration Test ==="
	@./simulator tests/test_basic.s
	@echo "=== All Tests Passed Successfully! ==="

clean:
	rm -f $(COMMON_OBJS) src/simulator/four_stage_runner.o src/simulator/six_stage_runner.o src/simulator/main.o simulator assembler

.PHONY: all test clean
