#include "four_stage_latches.hpp"
#include "debugger_pipeline_adapter.hpp"

#include <cassert>
#include <iostream>

int main() {
    IF_ID_Latch ifid;
    ID_EX_Latch idex;
    EX_MWB_Latch exmwb;

    ifid.is_valid = true;
    ifid.pc = 4;
    ifid.ir = 0x12345678;
    ifid.pc_next = 5;
    idex.is_valid = true;
    idex.rd = 3;
    idex.op1 = 11;
    idex.op2 = 22;
    idex.control[Signal::IS_ADD] = true;
    exmwb.is_valid = true;
    exmwb.exResult = 33;
    exmwb.is_Ebreak = true;

    DebuggerSnapshot snapshot;
    setDebuggerPC(snapshot, 4, 4);
    appendFourStagePipeline(snapshot, ifid, idex, exmwb, 4);

    assert(snapshot.pc == 16);
    assert(snapshot.pipeline.size() == 3);
    assert(findPipelineStage(snapshot, "IF/ID") != nullptr);
    assert(findPipelineField(snapshot, "IF/ID", "PC")->value == 16);
    assert(findPipelineField(snapshot, "ID/EX", "OP2")->value == 22);
    assert(findPipelineField(snapshot, "ID/EX", "IS_ADD")->value == 1);
    assert(findPipelineField(snapshot, "EX/MWB", "IS_EBREAK")->value == 1);

    std::cout << "Four-stage adapter test passed.\n";
    return 0;
}
