# Tracing a problem: `DXMT_TRACE_<CATEGORY>`

Each category of a problem has its own variable. `1` logs only what is out of the ordinary in it, at little cost, and
turns on nothing that costs the GPU. `heavy` logs every event of the category and turns on its slow modes. Turn on only
the categories the problem needs: heavy modes slow a game noticeably and write large logs.

```bash
DXMT_TRACE_GPU=1 DXMT_TRACE_RESOURCE=1 wine game.exe
```

| Variable | Use it for | `1` logs | `heavy` adds |
|---|---|---|---|
| `DXMT_TRACE_GPU` | device removal, GPU hangs, GPU errors | every command buffer that fails, on every queue (Metal reports the first of a GPU fault, and those after it as innocent victims), device removal, and a command buffer that has not completed after 2 s (`DXMT_TRACE_GPU_HANG_MS`) | each failed command buffer's passes and what became of each, and the most its indirect commands asked for (`DXMT_D3D12_GPU_ERRORS`; the GPU records them as it runs); every command buffer's commit and completion |
| `DXMT_TRACE_SYNC` | hangs and deadlocks | a queue `Wait` that holds the queue's work back | every `Signal`, `Wait`, `ExecuteCommandLists` and fence completion |
| `DXMT_TRACE_RESOURCE` | resources made wrong, tiled resources | reserved resources, failed resource creation, `UpdateTileMappings` and `CopyTileMappings` with their regions and ranges, and what each queue has on the GPU when a tile mapping is made | every committed and placed resource; every range of a tile mapping |
| `DXMT_TRACE_MEMORY` | out of memory, paging | heaps, and every 5 s the process's memory footprint and the system's swap and paging | every `QueryVideoMemoryInfo` |
| `DXMT_TRACE_SHADER` | stutter, shaders that fail | pipelines that take 20 ms or more to make, or are made after the first present | every pipeline and its time; the shaders, written to `shaders/` |
| `DXMT_TRACE_PRESENT` | frame pacing | a present 50 ms or more after the one before | every present |
| `DXMT_TRACE_PASS` | wrong render passes | | every Metal pass with its pipelines; D3D11 render passes with their size and draws (`DXMT_TRACE_ENCODERS`) |
| `DXMT_TRACE_API` | the call that fails | DXMT's info and debug lines | its trace lines |
| `DXMT_TRACE_CPU` | low frame rates | DXMT's profile records (`DXMT_DIAG=profile`), written next to the log | |

While any category is on, the log also has DXMT's warnings and errors, whatever `DXMT_LOG_LEVEL` is: a game's own
output often goes nowhere.

## Where the log goes

`~/Library/Logs/DXMT/<executable>/dxmt.log`, or `dxmt.log` in the folder `DXMT_TRACE_DIR` names (a macOS
path). Each line starts with the milliseconds since the log was opened, then the category and the Windows thread.
Each launch keeps the previous launch's log as `dxmt.prev.log`. Lines reach the disk within a second; a GPU line, a
warning or an error at once, so a process that hangs or is ended right after still has them.

The flight recorder (`<executable>.flight` in the same `~/Library/Logs/DXMT`) is separate and always on: it keeps a
process's last events in memory and writes them on a crash, a removed device or a stuck command buffer.
