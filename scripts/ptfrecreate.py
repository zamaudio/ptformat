#!/usr/bin/env python3
#-*- coding: utf-8 -*-

import re
import argparse
import sys
import os
import subprocess
import shutil

def get_args():  # pragma: no cover
    parser = argparse.ArgumentParser(
        description="pad audio files to recreate a protools session " +
        "in another DAW",
        add_help=True)
    parser.add_argument("ptx", action="store",
                        help="ptx file")
    parser.add_argument("ptx_audio_dir", action="store",
                        help="path to directory containing audio " +
                        "referenced by protools file")
    parser.add_argument("output_dir", action="store",
                        help="path to desired output dir")
    parser.add_argument("-x", "--exe", action="store",
                        help="location of ptxtool binary")
    parser.add_argument("-t", "--trackn", action="store",
                        help="only process this track number",
                        type=int)
    return parser.parse_args()


def run_ptxtool(args):
    if args.exe is None:
        args.exe = shutil.which("ptftool")
        if args.exe is None:
            args.exe = "./ptftool"

    results = "{args.output_dir}coords".format(**locals())
    cmd = "{args.exe} {args.ptx} > {results}".format(**locals())
    sys.stderr.write(cmd + "\n")
    subprocess.run([cmd],
                   shell=sys.platform != "win32",
                   stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE,
                   check=True)
    return results


def get_coords_of_interst_per_track(path, trackn):
    capture = False
    coords = {} # {trackn: [name, wav, abse, into, len]}

    with open(path, "r") as coords_file:
        for line in coords_file:
            if capture:
                #afiltered = filtered, lin
                thistrackn = int(line.split("(")[1].replace(")", ""))
                if trackn is not None and thistrackn != trackn:
                    continue
                track = line.split("`")[1].replace(" ", "_")
                wav = line.split("(")[2].replace(")", "").split("@")[0].strip()
                abso = int(line.split("@")[1].split(" ")[1])
                into = int(line.split("@")[1].replace(",", "").split(" ")[3])
                leng = int(line.split("@")[1].split(" ")[4].strip())


                if thistrackn in coords.keys():
                    coords[thistrackn].append([track, wav, abso, into, leng])
                else:
                    coords[thistrackn] = [[track, wav, abso, into, leng]]
            if not capture:
                if "Track name (Track#) (WAV filename) @ Absolute + Into-sample, Length:" == line.strip():
                    capture = True
            if "" == line:
                capture = False
    return coords


def make_sox_cmds(args, coords):
    cmds = []
    sox_exe = shutil.which("sox")
    for k, v in coords.items():
        for region in v:
            #print(region)
            # puts toether the cmd: sox 'input_file' output_file -p 552s || echo "error"
            cmd = str(
                sox_exe + " '" +
                os.path.join(args.ptx_audio_dir, region[1]) + "' " +
                os.path.join(args.output_dir,
                             "\(" + str(k) + "\)" +
                             region[0] + "__" +
                             region[1].replace(" ", "")
                ) + " pad " + str(
                    region[2] - region[3]) +
                "s || echo \"failure to pad all takes for track " +
                region[0]) + "\""
            sys.stdout.write(cmd + "\n")


def main():
    if shutil.which("sox") is None:
        raise ValueError("Sox must be in PATH")
    args = get_args()
    # in case comeone forgets trailing /
    args.output_dir = os.path.join(args.output_dir, "")
    os.makedirs(args.output_dir)
    coords_file = run_ptxtool(args)
    coords = get_coords_of_interst_per_track(
        path=coords_file, trackn=args.trackn)
    make_sox_cmds(args, coords)


if __name__ == "__main__":
    main()
