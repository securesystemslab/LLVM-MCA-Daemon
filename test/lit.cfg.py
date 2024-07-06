import lit.formats
import os

config.name = "Example tests"
config.test_format = lit.formats.ShTest(True)

config.suffixes = [".s"]

# test_source_root: The root path where tests are located.
config.test_source_root = os.path.dirname(__file__)

# Build directory path
mcad_bin_path = os.path.realpath("../build")
config.environment["MCAD_BIN_PATH"] = mcad_bin_path

# test_exec_root: The root path where tests should be run.
config.test_exec_root = mcad_bin_path

# Tweak the PATH to include the binary dir.
path = os.environ["PATH"] + os.pathsep + mcad_bin_path
config.environment["PATH"] = path
