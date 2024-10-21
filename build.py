import os
from os.path import join
from subprocess import check_call

if __name__ == '__main__':
    SKIA_SOURCE_DIR = r'C:\OldNew\Graphics-Lab\OpenHarmony\skia'
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
        'cmake': [
            '--ide=json',
            '--json-ide-script=../../gn/gn_to_cmake.py',
        ],
        'vs': [
            '--ide=vs',
        ],
    }

    # TODO
    shared = True
    cpu_os = 'x64'
    build_ide = 'gn'
    compiler = 'clang'
    single_threaded = False

    args.append('is_component_build=' + ('true' if shared else 'false'))
    args += cpu_os_args[cpu_os]
    dir = 'Shared' if shared else 'Static'
    dir += '-' + cpu_os
    dir += '-' + build_ide
    args += compiler_args[compiler]

    # concat args and execute
    args = '--args=' + (' '.join(args) if len(args) > 0 else '') + ''
    call_args = [join(SKIA_SOURCE_DIR, 'bin', 'gn'), 'gen', 'out/' + dir, args]
    call_args += build_args[build_ide]
    print(call_args)

    check_call(
        call_args,
        shell=True,
        cwd=SKIA_SOURCE_DIR,
    )

    if build_ide == 'cmake':
        call_args = ['cmake', '.']
    else:
        call_args = ['ninja', '-C', join(SKIA_SOURCE_DIR, 'out/' + dir)]
        if single_threaded:
            call_args += ['-j 1']
    check_call(
        call_args,
        shell=True,
        cwd=join(SKIA_SOURCE_DIR, 'out', dir),
    )
    