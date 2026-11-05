#include "six_stage_latches.hpp"
#include "debugger_pipeline_adapter.hpp"

#include <cassert>
#include <iostream>

int main() {
    IF_ID_Latch ifid;
    ID_EX_Latch idex;
    EX_MA1_Latch exma1;
    MA1_MA2_Latch ma1ma2;
    MA2_WB_Latch ma2wb;

    ifid.is_valid = true;
    ifid.pc = 1;
    ifid.ir = 0x87654321;
    ifid.pc_next = 2;
    idex.is_valid = true;
    idex.rd = 6;
    idex.op1 = 44;
    idex.control[Signal::IS_SUB] = true;
    exma1.is_valid = true;
    exma1.exResult = 55;
    ma1ma2.is_valid = true;
    ma1ma2.stack_fault = true;
    ma2wb.is_valid = true;
    ma2wb.read_data = 66;

    DebuggerSnapshot snapshot;
    appendSixStagePipeline(snapshot, ifid, idex, exma1, ma1ma2, ma2wb, 4);

    assert(snapshot.pipeline.size() == 5);
    assert(findPipelineField(snapshot, "IF/ID", "PC")->value == 4);
    assert(findPipelineField(snapshot, "ID/EX", "IS_SUB")->value == 1);
    assert(findPipelineField(snapshot, "EX/MA1", "EXRESULT")->value == 55);
    assert(findPipelineField(snapshot, "MA1/MA2", "STACK_FAULT")->value == 1);
    assert(findPipelineField(snapshot, "MA2/WB", "READ_DATA")->value == 66);

    std::cout << "Six-stage adapter test passed.\n";
    return 0;
}
