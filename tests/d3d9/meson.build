lib_d3d9 = cpp.find_library('d3d9')

executable('d3d9_clear', ['d3d9_clear.cpp'],
  dependencies: [ lib_d3d9 ],
  link_args: [ '-mwindows' ],
)

executable('d3d9_readback', ['d3d9_readback.cpp'],
  dependencies: [ lib_d3d9 ],
  link_args: [ '-mwindows' ],
)

executable('d3d9_resources', ['d3d9_resources.cpp'],
  dependencies: [ lib_d3d9 ],
  link_args: [ '-mwindows' ],
)

lib_d3dcompiler_d3d9 = cpp.find_library('d3dcompiler_47')
executable('d3d9_draw', ['d3d9_draw.cpp'],
  dependencies: [ lib_d3d9, lib_d3dcompiler_d3d9 ],
  link_args: [ '-mwindows' ],
)

executable('d3d9_states', ['d3d9_states.cpp'],
  dependencies: [ lib_d3d9, lib_d3dcompiler_d3d9 ],
  link_args: [ '-mwindows' ],
)

executable('d3d9_fixed_function', ['d3d9_fixed_function.cpp'],
  dependencies: [ lib_d3d9, lib_d3dcompiler_d3d9 ],
  link_args: [ '-mwindows' ],
)

executable('d3d9_present', ['d3d9_present.cpp'],
  dependencies: [ lib_d3d9 ],
  link_args: [ '-mwindows' ],
)

executable('d3d9_queries', ['d3d9_queries.cpp'],
  dependencies: [ lib_d3d9 ],
  link_args: [ '-mwindows' ],
)

executable('d3d9_state_blocks', ['d3d9_state_blocks.cpp'],
  dependencies: [ lib_d3d9 ],
  link_args: [ '-mwindows' ],
)

executable('d3d9_ps1', ['d3d9_ps1.cpp'],
  dependencies: [ lib_d3d9 ],
  link_args: [ '-mwindows' ],
)
