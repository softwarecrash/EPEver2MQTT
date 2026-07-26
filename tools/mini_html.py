Import("env")
import os
import glob
from pathlib import Path
import sys
import pip
import subprocess
import pkg_resources

def ensure_module_version(package_name, required_version):
    try:
        installed_version = pkg_resources.get_distribution(package_name).version
        if installed_version == required_version:
            print(f"{package_name} {required_version} is already installed.")
            return
        else:
            print(f"{package_name} version {installed_version} found – replacing with {required_version}.")
    except pkg_resources.DistributionNotFound:
        print(f"{package_name} is not installed – installing version {required_version}.")

    # Install the required version (will upgrade or downgrade as needed)
    subprocess.check_call(['pip', 'install', '--upgrade', f'{package_name}=={required_version}'])


ensure_module_version("minify_html", "0.15.0")
import minify_html

filePath = 'src/webpages/'

try:
  print("==========================")
  print("Generating webpage")
  print("==========================")
  print("Preparing html.h file from source")
  print("  -insert header") 
  cpp_output = "#pragma once\n\n#include <Arduino.h>  // PROGMEM\n\n"
  print("  -insert html")

  html_files = sorted(glob.glob(filePath+"*.html"))
  with open(filePath+"HTML_HEAD.html", "r", encoding="utf-8") as head_file:
    head_template = head_file.read()
  with open(filePath+"HTML_FOOT.html", "r", encoding="utf-8") as foot_file:
    foot_template = foot_file.read()

  for x in html_files:
   if Path(x).stem in ("HTML_HEAD", "HTML_FOOT"):
    continue
   print("prozessing file:" + Path(x).stem)
   print(Path(x).stem)
   cpp_output += "static const char "+Path(x).stem+"[] PROGMEM = R\"rawliteral("
   f = open(x, "r", encoding="utf-8")
   page = f.read()
   page = page.replace("%pre_head_template%", head_template)
   page = page.replace("%pre_foot_template%", foot_template)
   if env.GetProjectOption("build_type") == "debug":
        cpp_output += page
   else:
      cpp_output += minify_html.minify(page, minify_js=True)

   f.close()
   cpp_output += ")rawliteral\";\n"

   f = open ("./src/html.h", "w", encoding="utf-8")
   f.write(cpp_output)
   f.close()
   print("==========================\n")

except SyntaxError as e:
  print(e)
