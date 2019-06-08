#!/usr/bin/env node

const filename = process.argv[2];

const fs = require('fs');
const path = require('path');

/** @type {Buffer} */
const file = fs.readFileSync(filename);
// gets buffer

// check header
console.log('bitcode', file.toString('ascii',1,17));
if (file[0] !== 0x03 || file.toString('ascii',1,17) !== "0010111100101011") {
  console.log("Not a protools file");
  return
}

const isBigEndian = !!file[17]; // 0 if LE, 1 if BE

function makeHexChar (byte) {
  let b = byte.toString(16);
  return b.length === 1 ? "0" + b : b;
}

function makeHexString (...nums) {
  var s = nums.map(makeHexChar);
  return `0x${s.join("")}`;
}

function readBlockAt (buffer, pos, parentBlock) {
  if (buffer[pos] !== 0x5a) {
    console.log('weird, an unfinished block, breaking at ', pos);
    return;
  }
  const blockType = isBigEndian ? file.readUInt16BE(pos + 1) : file.readUInt16LE(pos + 1);
  const blockSize = isBigEndian ? file.readUInt32BE(pos + 3) : file.readUInt32LE(pos + 3);
  const contentType = isBigEndian ? file.readUInt16BE(pos + 7) : file.readUInt16LE(pos + 7);
  const totalBlockSize = 1 + 2 + 4 + blockSize;

  const blockTypeString = isBigEndian ?
    makeHexString(file.readUInt8(pos + 2), file.readUInt8(pos + 1)) :
    makeHexString(file.readUInt8(pos + 1), file.readUInt8(pos + 2)) ;
  const contentTypeString = isBigEndian ?
    makeHexString(file.readUInt8(pos + 8), file.readUInt8(pos + 7)) :
    makeHexString(file.readUInt8(pos + 7), file.readUInt8(pos + 8));

  const childBlocks = [];
  const blockContent = buffer.slice(pos + 1 + 2 + 4, blockSize);
  const ret = {
    pos,
    blockType, blockSize, totalBlockSize,
    contentType, blockTypeString, contentTypeString,
    blockContent, childBlocks,
    parentBlock
  };
  // now we recursively dive down the block
  let contentPos = buffer.indexOf(0x5a, pos + 1 + 2 + 4);
  while (contentPos !== -1 && contentPos < pos + totalBlockSize) {
    let b = readBlockAt (buffer, contentPos, ret);
    if (b) {
      childBlocks.push(b);
      contentPos = contentPos + b.totalBlockSize;
    }
    else contentPos += 1;
    contentPos = buffer.indexOf(0x5a, contentPos);
  }
  return ret;
}





// 18 and 19 are some bytes of which is unclear what they mean
// then at 20 we get the first 5a, we split everything up in blocks from there
const blocks = [];
let i = 20;
let done = false;
do  {
  let block = readBlockAt(file, i);
  if (!block) {
    break;
  }
  blocks.push(block);
  i += block.totalBlockSize || 1; // one for marker, two for type, 4 for block size int
} while (i < file.length)


var totalBlockAmount = blocks.reduce(function r (prev, next) {
  return prev + 1 + next.childBlocks.reduce(r, 0);
}, 0);

console.log("number of top level blocks", blocks.length);
console.log('total amount of blocks', totalBlockAmount);

const knownTypes = {
  "0x3000": "product info, including version",
  "0x0410": "audio file list, always block type 2",
  "0x1a27": "only occurs in 12, contains 'Markers'",
  "0x2810": "session sample rate",
  "0x6720": "session info, path of session",
  // "0x1925": ""
  "0x1125": "Snaps block"
  // "0x5620":
}




// Object.assign(require('repl').start('PT>>').context, {
//   blocks,
//   makeHexString,
// })



// blocks.forEach(b => {
//   const fp = path.join("blocks", b.blocktype.toString(16), b.blockcontenttypeString);
//   try {
//     fs.mkdirSync(fp, { recursive: true })
//   }
//   catch (e) {
//     if (e.code !== "EEXISTS") throw e;
//   }
//   const fn = path.join(fp, filename + "_" + b.loc);
//   fs.writeFileSync(fn, b.blockdata);
// });


/*


block type 3 seems to always be followed by a 16 bit int, 8 bit int and a 32 bit int


in 7.3
block type 5, content type 0x2810 LE, always followed by 0x0310 in both BE and LE
then a 32 bit int with the session rate



*/


/*
Content types: (hexadecimals in LE notation, as it easier to locate in a hex viewer)
0x30 00 => product info, including version
0x04 10 => audio file list, always block type 2
0x1a 27 => only occurs in 12, contains "Markers"
0x28 10 => session sample rate
0x67 20 => session info, path of session
0x19 25 =>
0x11 25 => Snaps block
0x56 20
*/

/*
  block type 2, content type 0x04 10: Audio file list
  uint32 num sound files
  then contains a block of type 1, content type 0x3a10
    uint32 num1
    uint8 some byte
    uint32 num2
    uint32 numchars (always 11, because audio files)
    char[11] => Audio Files
    uint32 => empty, unclear

    -> the following is repeated for every audio file
    uint8 => 0x02, seems to be indicator that something will be following
    uint32 => empty, unclear
    uint32 => length of the string to follow
    uint32 => number that reads WAVE in BE, EVAW in LE

    ->
    uint8 => 0x00 seems to be indicator that this was the last file
    uint32 => 0xFF FF FF FF, seems to be indicator for end of list

    uint32 => length of string to follow
    char[] => root drive name
    5 x uint8 => unclear what this is for
    uint32 => number, unclear
    uint32 => length of string to follow
    char[]
    5 x uint8 => unclear what this is for
    uint32 => number, unclear, is value of previous directory + 1
    uint32 => length of string to follow
    char[]
    5 x uint8 => unclear what this is for
    uint32 => number, unclear, is value of previous directory + 1
    uint32 => length of string to follow
    char[]

    after last path uint32 empty, perhaps to indicate that the end of the path has been reached

  Next block
  0x5a, block type 0x18 00 in 7.3, 0x0700 in v8
  content type 0x0310 Meta data of audio files
    uint32 // looks like a counter, data for file 1
    uint8 0x5a => content block:
      block type 0x0200
      uint32 block size
      content type 0x01 10
      uint32 sample rate of file ?
      uint8 very often 0x01
      uint8 very often 0x10, in pt8 0x18
      uint64 file length


*/

/*
  sample offset...
  block content type 0x0b10 LE
  uint32 possible number of blocks to follow
  nested block
  uint8 0x5a
  uint16 0x0600 => block type (7.1 has 5)
  uint32 block size
  uint16 content type 0x0810
    nested block
    uint8 0x5a
    uint16 block type 0x0c00
    uint32 block size
    uint16 content type 0x0710
    uint32 string length
    char[] name
    uint16 empty?
    uint16 number, sometimes 0x1000 sometimes 0x2000, is uint16 as compared to BE
    uint8 seems to be always zero
    uint16 length

*/

/*
Regions per track
block type 0x02 00
content type 0x5210
uint32 string length
char[]
uint32 regions per track

block 0x5a
block type 1
content type 0x5010
  nested block 0x5a
  block type 7
  content type 0x4f10
  uint16 number
  uint32 tr.reg.index
  uint32 tr.reg.offset


*/
