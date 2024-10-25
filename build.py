import distutils.file_util
import os
from os import path
from subprocess import check_call
import distutils.dir_util

def copy_libs(source_dir, target_dir):
    sufixs = {
        '.lib',
        '.dll',
        '.pdb',
        '.exp',
        '.ilk',
        '.a',
        '.so',
        '.dylib',
    }
    for file in os.listdir(source_dir):
        for sufix in sufixs:
            if file.endswith(sufix):
                src = path.join(source_dir, file)
                distutils.file_util.copy_file(src, target_dir, update=True)
                break


if __name__ == '__main__':
    CLANG_DIR = r'C:\dev\vcpkg\installed\x64-windows\tools\llvm'
    
    args = [
        # 'skia_use_system_libpng=false',
        # 'skia_use_system_libjpeg_turbo=false',
        # 'skia_use_system_zlib=false',
        # 'skia_use_system_expat=false',
        # 'skia_use_system_icu=false',
        # 'skia_use_system_libwebp=false',
        # 'skia_use_system_harfbuzz=false',
        # 'is_official_build=true',
        'is_debug=true',
    ]
    cpu_os_args = {
        'ohos': [
            # r'ndk="C:\dev\OpenHarmony\Sdk\12\native"',
            'target_cpu="arm64"',
            # 'target_os="android"',
            # 'is_android=false',
        ],
        'x64': [
            'target_cpu="x64"',
        ],
        'x86': [
            'target_cpu="x86"',
        ],
        'None': [],
    }
    compiler_args = {
        'msvc': [],
        'clang': [
            r'clang_win="' + CLANG_DIR +r'"'
        ],
    }
    build_args = {
        'gn': [],
        # 'cmake': [
        #     '--ide=json',
        #     '--json-ide-script=../../gn/gn_to_cmake.py',
        # ],
        # 'vs': [
        #     '--ide=vs',
        # ],
    }

    # configs
    is_component_build = True
    cpu_os = 'x64'
    build_chain = 'gn'
    compiler = 'clang'
    single_threaded = False

    args.append('is_component_build=' + ('true' if is_component_build else 'false'))
    args += cpu_os_args[cpu_os]
    dir = 'Shared' if is_component_build else 'Static'
    dir += '-' + cpu_os
    dir += '-' + build_chain
    args += compiler_args[compiler]

    # concat args and execute
    args = '--args=' + (' '.join(args) if len(args) > 0 else '') + ''
    call_args = [path.join('bin', 'gn'), 'gen', 'out/' + dir, args]
    call_args += build_args[build_chain]
    print(call_args)

    check_call(
        call_args,
        shell=True,
    )

    output_dir = path.join('out', dir)
    call_args = ['ninja', '-C', output_dir]
    if single_threaded:
        call_args += ['-j 1']
    check_call(
        call_args,
        shell=True,
    )

    # copy libs
    copy_libs(output_dir, 'lib')
    