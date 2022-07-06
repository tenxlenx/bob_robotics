import os
import sys

import skbuild

this_dir = os.path.dirname(__file__)
sys.path.append(os.path.join(this_dir, os.pardir))

from build_common import get_git_version


def build(setup_kwargs):
    """Build C-extensions."""

    version = get_git_version()
    cmake_args = ["-DBOB_VERSION=" + version]

    # The ranges library appears not to work with MSVC unless experimental C++20
    # features are enabled. (Tested with Visual Studio 2019.)
    if os.name == 'nt':
        cmake_args.append("-DCMAKE_CXX_STANDARD=20")

    setup_kwargs['cmake_args'] = cmake_args

    skbuild.setup(**setup_kwargs, script_args=["build_ext"])

if __name__ == "__main__":
    build({})
