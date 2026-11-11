import faulthandler, runpy, sys
faulthandler.dump_traceback_later(6, exit=True)
sys.argv = ["mkv_probe", sys.argv[1]]
runpy.run_path("verify/tools/mkv_probe.py", run_name="__main__")
