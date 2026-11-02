# the D3D12 performance probe, of the build's machine: a benchmark that perf_probe.py runs, not a test
executable('d3d12_perf_probe', 'd3d12_perf_probe.cpp', dependencies: d3d12_test_deps + [cpp.find_library('user32')])
# renders with no window or swap chain that bench.py times and profiles
executable('d3d12_render_bench', 'd3d12_render_bench.cpp', dependencies: d3d12_test_deps)
