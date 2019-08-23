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
    parser.add_argument("--dontrun",
                        help="dont run sox commands, just send to stdout", action="store_true")
    parser.add_argument("--check",
                        help="some projects appear to have some regions aligned with the wrong tracks. The default behaviour for this is to check is the start of the region name is similar enough to the start of the clip name.  This DEFINITELY will be problematic if you rename your tracks so that the regions dont match the region.", action="store_true")
    parser.add_argument("--add",
                        help="will use existing --output_dir if it exists",
                        action="store_true")
    return parser.parse_args()


def run_ptxtool(args):
    if args.exe is None:
        args.exe = shutil.which("ptftool")
        if args.exe is None:
            args.exe = "./ptftool"

    results = "{args.output_dir}coords".format(**locals())
    cmdl = [args.exe, args.ptx, ">", results]
    if os.path.exists(results):
        if os.path.getsize(results) == 0:
            sys.stderr.write("coords file empty! exiting\n")
            sys.exit(1)
        sys.stderr.write("Using existing coords file\n")
    else:
        sys.stderr.write(" ".join([str(x) for x in cmdl])+ "\n")
        subprocess.run(cmdl,
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
                track = line.split("`")[1]
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
        for i, region in enumerate(v):
            #print(region)
            # puts toether the cmd: sox 'input_file' output_file -p 552s || echo "error"
            cmd = str(
                sox_exe + ' "' +
                os.path.join(args.ptx_audio_dir, region[1]) + '" "' +
                os.path.join(args.output_dir,
                             "(" + str(k) + ")" +
                             region[0].replace(" ", "_") + "__" + str(i) + "_" +
                             region[1].replace(" ", "")
                ) + '" pad ' + str(
                    region[2] - region[3]) +
                "s"
                )
            cmds.append(cmd)
    return cmds


def check_coords(coords):
    # removes regions that may be misassigned.  This
    new_coords = {}
    nregions = 0
    goodregions = 0
    bad = []
    for k, v in coords.items():
        #print(k)
        #print(v)
        for region in v:
            nregions = nregions + 1
            track_base = region[0].replace(" ", "_").split(".")[0].split(" ")[0].strip()
            #print(track_base)
            wav_base = region[1].replace(" ", "_").split(".")[0].split(" ")[0].strip()
            #print(wav_base)
            if not wav_base.startswith(track_base):
                bad.append(region)
            else:
                goodregions = goodregions + 1
                if k in new_coords.keys():
                    new_coords[k].append(region)
                else:
                    new_coords[k] = [region]
    if bad:
        sys.stderr.write("rejecting the following regions:\n")
        for b in bad:
            sys.stderr.write("\t".join([str(x) for x in b]) + "\n")
    sys.stderr.write("retained %i of %i regions\n" % (goodregions, nregions))
    return new_coords

def main():
    if shutil.which("sox") is None:
        raise ValueError("Sox must be in PATH")
    args = get_args()
    # in case comeone forgets trailing /
    args.output_dir = os.path.join(args.output_dir, "")
    try:
        os.makedirs(args.output_dir)
    except:
        if not args.add:
            sys.stderr.write("Output directory already exists!\n")
            sys.exit(1)
    coords_file = run_ptxtool(args)
    coords = get_coords_of_interst_per_track(
        path=coords_file, trackn=args.trackn)
    if args.check:
        coords = check_coords(coords)
    cmds = make_sox_cmds(args, coords)
    ncmds = len(cmds)
    if not args.dontrun:
        sys.stderr.write("running %i sox cmds...\n" % ncmds)
        for i, cmd in enumerate(cmds):
            if ((i + 1) % 25) == 0:
                sys.stderr.write("  %i of %i\n" %(i + 1, ncmds))
            subprocess.run([cmd],
                           shell=sys.platform != "win32",
                           stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE,
                           check=True)
    else:
        for i, cmd in enumerate(cmds):
            sys.stdout.write(cmd + "\n")



if __name__ == "__main__":
    main()
