import lldb
import os

def __lldb_init_module(debugger, internal_dict):
    dbgcall("command script add -f lldbinit.make make")
    dbgcall("command script add -f lldbinit.graph graph")
    dbgcall("command script add -f lldbinit.cleandot cleandot")
    dbgcall("command script add -f lldbinit.diagrams diagrams")
    dbgcall("command script add -f lldbinit.plog plog")
    dbgcall("b throw_error");
    return

def dbgcall(command):
    res = lldb.SBCommandReturnObject()
    lldb.debugger.GetCommandInterpreter().HandleCommand(command, res)
    return res.GetOutput()


def make(debugger, command, result, dict):
    os.system("make -j -s BUILD=debugger eval")
    dbgcall("target delete")
    dbgcall("target create \"eval\"")
    dbgcall("b throw_error");

def graph(debugger, command, result, dict):
    dbgcall("p make_graph_all(0)")

def cleandot(debugger, command, result, dict):
    os.system("make clean-dot")

def diagrams(debugger, command, result, dict):
    os.system("make diagrams")

def plog(debugger, command, result, dict):
    dbgcall("p log_print_all()")
