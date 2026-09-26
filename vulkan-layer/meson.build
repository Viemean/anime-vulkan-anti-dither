project('wuwa_vulkan_layer', 'cpp',
  version : '1.0.0',
  meson_version : '>= 1.1.0',
  default_options : [
    'warning_level=2',
    'cpp_std=c++20',
    'b_ndebug=if-release',
    'b_lto=true',
    'buildtype=release'
  ])

vulkan_headers_dep = dependency('vulkan-headers', required : false)
if not vulkan_headers_dep.found()
  vulkan_headers_dep = dependency('VulkanHeaders', fallback : ['vulkan-headers', 'vulkan_headers_dep'])
endif

layer_cpp_args = ['-fvisibility=hidden']
if not get_option('logging')
  layer_cpp_args += ['-DDISABLE_LOGGING']
endif

wuwa_layer_lib = shared_library('wuwa_antidither_layer',
  'src/layer.cpp',
  dependencies : [vulkan_headers_dep],
  cpp_args : layer_cpp_args,
  link_args : ['-Wl,-Bsymbolic', '-static-libstdc++', '-static-libgcc'],
  install : true
)

anti_dither_test = executable('anti_dither_test',
  'test/test_anti_dither.cpp',
  dependencies : [vulkan_headers_dep],
  cpp_args : ['-O2']
)

test('anti_dither_test', anti_dither_test)

nte_anti_dither_test = executable('nte_anti_dither_test',
  'test/test_nte_anti_dither.cpp',
  dependencies : [vulkan_headers_dep],
  cpp_args : ['-O2']
)

test('nte_anti_dither_test', nte_anti_dither_test)

memory_patcher_test = executable('memory_patcher_test',
  'test/test_memory_patcher.cpp',
  dependencies : [vulkan_headers_dep],
  cpp_args : ['-O2']
)

test('memory_patcher_test', memory_patcher_test)

hsr_anti_dither_test = executable('hsr_anti_dither_test',
  'test/test_hsr_anti_dither.cpp',
  dependencies : [vulkan_headers_dep],
  cpp_args : ['-O2']
)

test('hsr_anti_dither_test', hsr_anti_dither_test)

genshin_anti_dither_test = executable('genshin_anti_dither_test',
  'test/test_genshin_anti_dither.cpp',
  dependencies : [vulkan_headers_dep],
  cpp_args : ['-O2']
)

test('genshin_anti_dither_test', genshin_anti_dither_test)

