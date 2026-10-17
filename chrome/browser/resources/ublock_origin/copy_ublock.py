#!/usr/bin/env python3
# Copyright 2026 The PolyBrowser Authors
# Copies bundled uBlock Origin extension into $root_out_dir/resources/ublock_origin.

import os
import shutil
import sys


def main():
  if len(sys.argv) < 4:
    return 1
  src_dir = sys.argv[1]
  dst_dir = sys.argv[2]
  stamp_file = sys.argv[3]

  if os.path.exists(dst_dir):
    shutil.rmtree(dst_dir)
  shutil.copytree(
      src_dir,
      dst_dir,
      ignore=shutil.ignore_patterns('copy_ublock.py', '__pycache__'),
  )
  os.makedirs(os.path.dirname(stamp_file), exist_ok=True)
  with open(stamp_file, 'w', encoding='utf-8') as f:
    f.write('ok\n')
  return 0


if __name__ == '__main__':
  sys.exit(main())

