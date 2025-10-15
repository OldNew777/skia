import distutils.file_util
import os
from os import path
from subprocess import check_call
import distutils.dir_util
import argparse
import sys
import shutil
import time

EPSILON = 10.0

def newer_than(src_dir, dst_dir):
    if not os.path.exists(dst_dir):
        return True
    for src_root, _, files in os.walk(src_dir):
        rel_root = os.path.relpath(src_root, src_dir)
        dst_root = os.path.join(dst_dir, rel_root)
        for f in files:
            src_file = os.path.join(src_root, f)
            dst_file = os.path.join(dst_root, f)
            if not os.path.exists(dst_file):
                return True
            if os.path.getmtime(src_file) - os.path.getmtime(dst_file) > EPSILON:
                return True
    return False

def smart_copy_incremental(src_dir, dst_dir, label):
    os.makedirs(dst_dir, exist_ok=True)
    for subdir in os.listdir(src_dir):
        src_subdir = os.path.join(src_dir, subdir)
        dst_subdir = os.path.join(dst_dir, subdir)
        if not os.path.isdir(src_subdir):
            continue
        if newer_than(src_subdir, dst_subdir):
            print(f">>>    Changes detected. Copying {label}{subdir}/...")
            if os.path.exists(dst_subdir):
                shutil.rmtree(dst_subdir)
            shutil.copytree(src_subdir, dst_subdir)
        else:
            print(f">>>    {label}{subdir}/ unchanged. Skipping copy.")

def smart_copy_modified_files(src_dir, dst_dir, label=""):
    copied_any = False
    for root, dirs, files in os.walk(src_dir):
        # 跳过 .git 目录
        dirs[:] = [d for d in dirs if d != '.git']

        for file in files:
            src_file = os.path.join(root, file)
            rel_path = os.path.relpath(src_file, src_dir)
            dst_file = os.path.join(dst_dir, rel_path)

            # 目标文件不存在，或者源文件时间更新，才复制
            if not os.path.exists(dst_file) or os.path.getmtime(src_file) > os.path.getmtime(dst_file):
                os.makedirs(os.path.dirname(dst_file), exist_ok=True)
                shutil.copy2(src_file, dst_file)
                print(f">>>    Copied: {os.path.join(label, rel_path)}")
                copied_any = True

    if not copied_any:
        print(f">>>    {label or src_dir} unchanged. Skipping copy.")


def str2bool(v):
    if v.lower() in ('yes', 'true', 't', 'y', '1', 'on'):
        return True
    elif v.lower() in ('no', 'false', 'f', 'n', '0', 'off'):
        return False
    else:
        raise argparse.ArgumentTypeError('Boolean value expected.')


def bool2str(v):
    return 'true' if v else 'false'


def copy_libs(parsed_args, source_dir, target_dir):
    os.makedirs(target_dir, exist_ok=True)
    suffix_platform = {
        'WIN': ['.lib', '.dll', '.pdb', '.exp', '.ilk'],
        'LINUX': ['.a', '.so'],
        'OHOS': ['.a', '.so'],
        'APPLE': ['.a', '.dylib'],
    }
    all_postfixes = set()
    for suffixes in suffix_platform.values():
        all_postfixes.update(suffixes)
    suffix_platform[''] = list(all_postfixes)

    suffixes = suffix_platform.get(parsed_args.target_os.upper(), suffix_platform[''])
    for file in os.listdir(source_dir):
        for suffix in suffixes:
            if file.endswith(suffix):
                src = path.join(source_dir, file)
                distutils.file_util.copy_file(src, target_dir, update=True)
                break


def parse_args():
    parser = argparse.ArgumentParser(description='Build Skia')
    parser.add_argument('--is_debug', type=str2bool, default=True, help='Build Skia as debug version')
    parser.add_argument('--is_component_build', type=str2bool, default=True, help='Build Skia as a component')
    parser.add_argument('--is_trivial_abi', type=str2bool, default=True, help='Build Skia with trivial ABI')
    parser.add_argument('--target_cpu', type=str, default='x64', help='Target CPU architecture')
    parser.add_argument('--target_os', type=str, default='WIN', help='Target OS (WIN, LINUX, OHOS, APPLE)')
    parser.add_argument('--build_chain', type=str, default='gn', help='Build chain')
    parser.add_argument('--compiler', type=str, default='clang', help='Compiler')
    parser.add_argument('--cc', type=str, default='', help='Path to clang')
    parser.add_argument('--cxx', type=str, default='', help='Path to clang++')
    parser.add_argument('--clang_win', type=str, default='none', help='Clang directory')
    parser.add_argument('--single_threaded_build', type=str2bool, default=False, help='Single threaded build')
    return parser.parse_args()


if __name__ == '__main__':
    parsed_args = parse_args()

    target_os = parsed_args.target_os.upper()

    if target_os == 'OHOS':
        parsed_args.target_cpu = 'arm64'

    gl_params = 'false'
    if target_os in ('LINUX', 'OHOS'):
        gl_params = 'true'

    args = [
        # 'skia_use_system_libpng=false',
        # 'skia_use_system_libjpeg_turbo=false',
        # 'skia_use_system_zlib=false',
        # 'skia_use_system_expat=false',
        # 'skia_use_system_icu=false',
        # 'skia_use_system_libwebp=false',
        # 'skia_use_system_harfbuzz=false',
        'is_official_build=false',
        'is_debug=' + bool2str(parsed_args.is_debug),
        'is_component_build=' + bool2str(parsed_args.is_component_build),
        'target_cpu="' + parsed_args.target_cpu + '"',
        'target_os="' + target_os.lower() + '"',
        'is_trivial_abi=' + bool2str(parsed_args.is_trivial_abi),
        'skia_enable_gpu=' + gl_params,
        'skia_use_gl=' + gl_params,
        'skia_use_egl=' + gl_params,
    ]

    if target_os == 'OHOS':
        args += [
            'skia_use_system_freetype2=false',
            'skia_enable_pdf=false',
            'skia_use_harfbuzz=false',
            'skia_use_icu=false',
            'extra_cflags=["-D__builtin_smulll_overflow(x,y,p)=(*(p)=(x)*(y),false)"]',
        ]

    compiler_args = {
        'msvc': [],
        'clang': [],
    }

    if target_os == 'WIN':
        assert parsed_args.clang_win != '', 'Clang directory must be set for Windows'
        compiler_args['clang'].append(r'clang_win="' + parsed_args.clang_win + r'"')

    build_args = {
        'gn': [],
    }

    # 输出目录
    dir = 'Shared' if parsed_args.is_component_build else 'Static'
    dir += '-' + parsed_args.target_cpu
    dir += '-' + parsed_args.build_chain
    output_dir = path.join('out', dir)

    # 编译器设置
    args += compiler_args[parsed_args.compiler]
    if parsed_args.cc:
        args.append(f'cc="{parsed_args.cc}"')
    elif target_os in ('LINUX', 'OHOS'):
        args.append('cc="clang"')

    if parsed_args.cxx:
        args.append(f'cxx="{parsed_args.cxx}"')
    elif target_os in ('LINUX', 'OHOS'):
        args.append('cxx="clang++"')

    # GN 参数拼接
    gn_args_str = '--args=' + ' '.join(args)

    print(f">>> Step 1: Generate GN files for {target_os}")
    gn_cmd = [path.join('bin', 'gn'), 'gen', output_dir, gn_args_str]
    gn_cmd += build_args[parsed_args.build_chain]
    check_call(gn_cmd, shell=False)

    print(f">>> Step 2: Ninja build for {target_os}")
    ninja_cmd = ['ninja', '-C', output_dir]
    if parsed_args.single_threaded_build:
        ninja_cmd += ['-j', '1']
    check_call(ninja_cmd, shell=False)

    print(f">>> Step 3: Copy output libs for {target_os}")
    copy_libs(parsed_args, output_dir, 'lib')

    print(f"✅ Skia build for {target_os} completed.")