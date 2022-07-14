import os
import random
import sys

from skbuild import setup

this_dir = os.path.dirname(__file__)
sys.path.append(os.path.join(this_dir, os.pardir))

from build_common import get_git_version

version = get_git_version()
cmake_args = ["-DBOB_VERSION=" + version]

# The ranges library appears not to work with MSVC unless experimental C++20
# features are enabled. (Tested with Visual Studio 2019.)
if os.name == 'nt':
    cmake_args.append("-DCMAKE_CXX_STANDARD=20")

build_id = random.getrandbits(32)
with open(os.path.join(this_dir, 'bob_robotics', 'navigation', 'build_id.py'), 'w') as file:
    file.write(f'# Generated by setup.py\n__build_id__ = "{build_id:05x}"\n')

setup(
    name="bob_navigation",
    version=version,
    description="A python interface for the BoB robotics navigation module",
    author='Alex Dewar',
    license="GPLv2",
    packages=['bob_robotics.navigation'],
    package_data = { 'bob_robotics.navigation': ['*.dll'] },
    cmake_args = cmake_args
)
