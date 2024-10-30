import distutils.file_util
import os
from os import path
from subprocess import check_call
import distutils.dir_util
import argparse
import sys

def copy_libs(parsed_args, source_dir, target_dir):
    os.makedirs(target_dir, exist_ok=True)
    suffix_platform = {
        'WIN': [
            '.lib',
            '.dll',
            '.pdb',
            '.exp',
            '.ilk',
        ],
        'LINUX': [
            '.a',
            '.so',
        ],
        'APPLE': [
            '.a',
            '.dylib',
        ],
        '': [
            '.lib',
            '.dll',
            '.pdb',
            '.exp',
            '.ilk',
            '.a',
            '.so',
            '.dylib',
        ]
    }
    if parsed_args.target_os in suffix_platform:
        suffixes = suffix_platform[parsed_args.target_os]
    else:
        suffixes = suffix_platform['']

    for file in os.listdir(source_dir):
        for suffix in suffixes:
            if file.endswith(suffix):
                src = path.join(source_dir, file)
                distutils.file_util.copy_file(src, target_dir, update=True)
                break

def parse_args():
    def str2bool(v):
        if v.lower() in ('yes', 'true', 't', 'y', '1', 'on'):
            return True
        elif v.lower() in ('no', 'false', 'f', 'n', '0', 'off'):
            return False
        else:
            raise argparse.ArgumentTypeError('Boolean value expected.')

    parser = argparse.ArgumentParser(description='Build Skia')
    parser.add_argument('--is_debug', type=str2bool, default=True, help='Build Skia as debug version')
    parser.add_argument('--is_component_build', type=str2bool, default=True, help='Build Skia as a component')
    parser.add_argument('--target_cpu', type=str, default='x64', help='CPU architecture')
    parser.add_argument('--target_os', type=str, default='WIN', help='CPU architecture')
    parser.add_argument('--build_chain', type=str, default='gn', help='Build chain')
    parser.add_argument('--compiler', type=str, default='clang', help='Compiler')
    parser.add_argument('--clang_win', type=str, default='', help='Clang directory')
    parser.add_argument('--single_threaded_build', type=str2bool, default=False, help='Single threaded build')
    return parser.parse_args()


if __name__ == '__main__':
    # configs
    parsed_args = parse_args()

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
    compiler_args = {
        'msvc': [],
        'clang': [],
    }
    if parsed_args.target_os == 'WIN':
        assert parsed_args.clang_win != '', 'Clang directory must be set for Windows'
        compiler_args['clang'].append(r'clang_win="' + parsed_args.clang_win +r'"')
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

    args.append('is_component_build=' + ('true' if parsed_args.is_component_build else 'false'))
    args.append('target_cpu="' + parsed_args.target_cpu + '"')
    dir = 'Shared' if parsed_args.is_component_build else 'Static'
    dir += '-' + parsed_args.target_cpu
    dir += '-' + parsed_args.build_chain
    args += compiler_args[parsed_args.compiler]

    # concat args and execute
    args = '--args=' + (' '.join(args) if len(args) > 0 else '') + ''
    call_args = [path.join('bin', 'gn'), 'gen', 'out/' + dir, args]
    call_args += build_args[parsed_args.build_chain]
    print(call_args)

    check_call(
        call_args,
        shell=True,
    )

    output_dir = path.join('out', dir)
    call_args = ['ninja', '-C', output_dir]
    if parsed_args.single_threaded_build:
        call_args += ['-j 1']
    check_call(
        call_args,
        shell=True,
    )

    # copy libs
    copy_libs(parsed_args, output_dir, 'lib')
    