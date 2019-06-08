#!/usr/bin/env node

if (process.argv.length === 2) {
  console.log(`read_unxored_ptformat: no file to read.

  Usage:
    read_unxored_ptformat [options] filename

  Options:
    --fullhex  Always show the hex, even if the layout is declared

  `);
  process.exit(0);
}


const filename = process.argv[process.argv.length - 1];
const showFullHex = process.argv.indexOf('--fullhex') !== -1;

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
  const blockContent = buffer.slice(pos + 1 + 2 + 4, pos + totalBlockSize);
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

/*
  layout: array of objects
  layout object:
    - type: number in case number of [number] bits. signed is in the separate signed property
      if type is string: it means: a 32bit unsigned int, telling length of string, followed
      by the string of that length.
      If type is 'path', it is assumed to be a series of strings, prepended by the amount
    - signed: false true, in case of number
    - name: field name
    - omit: true or false, whether to include the field in the displayed result

*/

const contentTypes = {
  "0x0300": {
    description: "product info, including version",
    layout: [
      { type: 16, signed: false, name: 'content type', omit: true },
      { type: 8, signed: false, name: 'unknown'},
      { type: 'string', name: 'Product name' },
      { type: 32, signed: false, name: 'possible version depths?'},
      { type: 32, signed: false, name: 'Major version'},
      { type: 32, signed: false, name: 'Minor version' },
      { type: 32, signed: false, name: 'Patch version' },
      { type: 'string', name: 'Version text'},
      { type: 8, signed: false, name: 'unknown' },
      { type: 'string', name: 'Release' },
      { type: 8, signed: false, name: 'unknown' },
      { type: 'string', name: 'File type name'},
      { type: 8, signed: false, name: 'unknown' },
      { type: 8, signed: false, name: 'unknown, possibly some OS indicator, as it differs per OS' },
      { type: 'string', name: 'Operating System'},
      { type: 32, signed: false, name: 'unknown' },
      { type: 8, signed: false, name: 'unknown' },
    ]
  },
  "0x0410": {
    description: "audio content list, always block type 2",
    // layout: [
    //   { type: 16, signed: false, name: 'content type', omit: true },
    //   { type: 32, signed: false, name: 'number of audio files' },
    // ]
    parser: function (block) {
      const buffer = block.blockContent;

      let numberOfSoundFiles = isBigEndian ? buffer.readUInt32BE(2) : buffer.readUInt32LE(2);
      // take the child having content type 0x3a10, as that contains the file lists
      const fileLists = block.childBlocks.find(cb => cb.contentTypeString === "0x3a10");
      // the file lists can contain both paths and files
      // 0x01 indicates a path, 0x02 indicates a file
      // probably done because of not having to save the full path for every file
      if (!fileLists) return [];
      const flbuffer = fileLists.blockContent;
      let pos = 6; // skipping the content type and counter
      let done = false;
      let currentDir = "";
      let previousType;
      let curType;
      let ret = [];
      while (pos < flbuffer.length && !done) {
        curType = flbuffer[pos];
        if (curType === 0x01) { // directory
          pos += 5; // skip counter
          const len = isBigEndian ? flbuffer.readUInt32BE(pos) : flbuffer.readUInt32LE(pos);
          const start = pos + 4;
          const end = start + len;
          if (previousType !== 0x01) {
            currentDir = flbuffer.toString('ascii', pos + 4, pos + 4 + len);
          }
          else if (previousType === 0x01) {
            currentDir += "/" + flbuffer.toString('ascii', pos + 4, pos + 4 + len);
          }
          pos = start + len + 4; // for some reason always an empty string
        }
        else if (curType === 0x02) {
          pos += 1;
          const fileType = isBigEndian ? flbuffer.readUInt32BE(pos) : flbuffer.readUInt32LE(pos);
          pos += 4;
          const len = isBigEndian ? flbuffer.readUInt32BE(pos) : flbuffer.readUInt32LE(pos);
          const start = pos + 4;
          const end = start + len;
          const filename = flbuffer.toString('ascii', start, end);
          ret.push({ path: currentDir + "/" + filename, fileType });
          pos = end + 4; // WAVE / AVEW / AIFF / FFAI
        }
        else {
          done = true;
        }
        previousType = curType;
      }
      // we should have now all file names as properties of ret
      // console.log(ret);
      // now go through the file meta data
      // these are in blocks with contentType 0x0310
      return ret.map(r => `path: ${r.path}, fileType: ${r.fileType}`);
    }
  },
  "0x0310": {
    description: "audio file meta data",
    layout: [
      { type: 16, signed: false, name: 'content type', omit: true },
      { type: 32, signed: false, name: 'file number, most likely in the order of file names' },
    ]
  },
  "0x0110": {
    description: "meta data block, file number is in the parent block",
    layout: [
      { type: 16, signed: false, name: 'content type', omit: true },
      { type: 32, signed: false, name: 'sample rate' },
      { type: 8, signed: false, name: 'unknown, always 0x01' },
      { type: 8, signed: false, name: 'bitness' },
      { type: 64, signed: false, name: 'number of samples / frames' },
      { type: 8, signed: false, name: 'unknown, always 0x03' },
      { type: 32, signed: false, name: 'unknown (empty)' },
      { type: 32, signed: false, name: 'unknown, always 0x2a' },
      { type: 32, signed: false, name: 'unknown (format unsure)' },
      { type: 32, signed: false, name: 'unknown (format unsure)' },
    ]
  },
  "0x3a10": {
    /*
      complex, it looks like there are two possible lists in here
      one is the internal list, but there is a list of non-imported files.
      it looks like 01 is used as marker to show that a list follows,
      what about the property "optional", so it can try, and if it doesn't
      work out, it skips to the next item
    */
    description: "audio file list",
    // layout: [
    //   { type: 16, signed: false, name: 'content type', omit: true },
    //   { type: 32, signed: false, name: 'unknown' },
    //   { type: 8, signed: false, name: 'unknown' }, // => marker whether list follows
    //   { type: 32, signed: false, name: 'unknown' },
    // ]

  },
  "0x1a27": {
    description: "only occurs in 12, contains 'Markers'",
  },
  "0x2810": {
    description: "session settings",
    layout: [
      { type: 16, signed: false, name: 'content type', omit: true },
      { type: 8, signed: false, name: 'unknown, always 0x03' },
      { type: 8, signed: false, name: 'bitness' },
      { type: 32, signed: false, name: 'sample rate'},
      { type: 8, signed: false, name: 'unknown, format unclear' },
      { type: 32, signed: false, name: 'unknown, format unclear' },
      { type: 8, signed: false, name: 'unknown, 0x01' },
      { type: 'path', name: "path to io settings file" },
      { type: 'string', name: 'io settings filename' },

    ]
  },
  "0x6720": {
    description: "session info, path of session",
    layout: [
      { type: 16, signed: false, name: 'content type', omit: true },
      { type: 32, signed: false, name: 'unknown 32bit' },
      { type: 32, signed: false, name: 'unknown 32bit, 0x2a 00 00 00' },
      { type: 16, signed: false, name: 'unknown, unclear whether size is right' },
      { type: 64, signed: false, name: 'unknown 64bit, unclear whether size is right' },
      { type: 32, signed: false, name: 'unknown 32bit, 0x0a 00 00 00' },
      { type: 'string', name: "Info1"},
      { type: 'string', name: "Info2" },
      { type: 'string', name: "Info3" },
      { type: 'string', name: "Info4" },
      { type: 'string', name: "Info5" },
      { type: 64, signed: false, name: 'unknown 64bit, always empty' },
      { type: 64, signed: false, name: 'unknown 64bit, always empty' },
      { type: 64, signed: false, name: 'unknown 64bit, always empty' },
      { type: 'path', name: "path to filename"},
      { type: 'string', name: 'filename'},
      { type: 32, signed: false, name: 'unknown 32bit, 0x00 00 00 00' },
      // it is possible that an original filename follows,
      // then next value is then 0x2a 00 00 00, after which another path and filename follow
      // there is more info in here, but it is a bit unclear what it means
    ]
  },
  // "0x1925": ""
  "0x1125": {
    description: "Snaps block"
  }
  // "0x5620":
}




// creates an array of field names plus values
function parseLayout (buffer, layout) {
  const ret = [];
  let pos = 0;
  let fieldValue;
  for (let field of layout) {
    switch (field.type) {
      case 8:
        fieldValue = field.signed? buffer.readInt8(pos): buffer.readUInt8(pos);
        pos += 1;
        break;
      case 16:
        if (isBigEndian) {
          fieldValue = field.signed ? buffer.readInt16BE(pos) : buffer.readUInt16BE(pos);
        }
        else {
          fieldValue = field.signed ? buffer.readInt16LE(pos) : buffer.readUInt16LE(pos);
        }
        pos += 2;
        break;
      case 32:
        if (isBigEndian) {
          fieldValue = field.signed ? buffer.readInt32BE(pos) : buffer.readUInt32BE(pos);
        }
        else {
          fieldValue = field.signed ? buffer.readInt32LE(pos) : buffer.readUInt32LE(pos);
        }
        pos += 4;
        break;
      case 64:
        if (isBigEndian) {
          fieldValue = field.signed ? buffer.readIntBE(pos, 8) : buffer.readUIntBE(pos, 8);
        }
        else {
          fieldValue = field.signed ? buffer.readIntLE(pos, 8) : buffer.readUIntLE(pos, 8);
        }
        pos += 8;
        break;
      case 'string':
        const len = isBigEndian? buffer.readUInt32BE(pos): buffer.readUInt32LE(pos);
        const start = pos + 4;
        const end = start + len;
        fieldValue = buffer.toString('ascii', pos+4, pos+4+len);
        pos = start + len;
        break;
      case 'path':
        const depth = isBigEndian ? buffer.readUInt32BE(pos) : buffer.readUInt32LE(pos);
        const p = [];
        pos += 4;
        for (let depth_i = 0; depth_i < depth; depth_i += 1) {
          const strlen = isBigEndian ? buffer.readUInt32BE(pos) : buffer.readUInt32LE(pos);
          pos += 4;
          p.push(buffer.toString('ascii', pos, pos + strlen));
          pos += strlen;
        }
        fieldValue = p.join("/");
        break;
      case 'filelist':
        const list = [];
        while (buffer[pos] === 0x02) {
          pos += 1;
          const ft = isBigEndian ? buffer.readUInt32BE(pos) : buffer.readUInt32LE(pos);
          pos += 4;
          const strlen = isBigEndian ? buffer.readUInt32BE(pos) : buffer.readUInt32LE(pos);
          list.push(buffer.toString('ascii', pos, pos + strlen) + ", filetype " + ft);
          pos += strlen;
        }
        // starts with 0x02, then what looks like a file type 0 for wav, 2 for aiff
    }
    if (!field.omit) ret.push(`${field.name}: \t ${fieldValue}`);
  }
  return ret;
}



// idea is to create a nested schema
function describeBlock (block, indentLevel) {
  const contentType = contentTypes[block.contentTypeString];
  const description = contentType? contentType.description: "";
  const buffer = block.blockContent;
  let hexData = [];
  let hexString = "";
  const maxWidth = 24;
  let stringData = "";
  let byte;
  const divider = "".padStart(4, " ");

  for (let i = 0; i < buffer.length; i += 1) {
    byte = buffer.readUInt8(i);
    hexString += makeHexChar(byte) + " ";
    stringData += byte >= 32 && byte <= 126 ? String.fromCharCode(byte) : ".";
    // stringData += buffer.toString('ascii', i);
    if (i % maxWidth === maxWidth - 1) {
      hexString += divider + stringData;
      hexData.push(hexString);
      hexString = "";
      stringData = "";
    }
    if (i === buffer.length - 1) {
      hexString = hexString.padEnd(24*3, " ") + divider + stringData;
      hexData.push(hexString);
    }
  }

  if (contentType) {
    if (contentType.parser) {
      const parsed = contentType.parser(block);
      hexData = showFullHex? parsed.concat(hexData): parsed;
    }
    else if (contentType.layout) {
      if (showFullHex) {
        hexData = parseLayout(buffer, contentType.layout).concat(hexData);
      }
      else hexData = parseLayout(buffer, contentType.layout);
    }
  }

  const template = [
    `Block type ${block.blockTypeString} (${block.blockType})`,
    `Block size ${block.blockSize}`,
    `Content type ${block.contentTypeString} (${block.contentType}) ${description}`,
  ].concat(hexData);
  const indent = "".padStart(indentLevel*2, " ");
  let ret = template.map(s => indent + s);
  if (block.childBlocks) {
    block.childBlocks.forEach(cb => {
      ret = ret.concat(describeBlock(cb, indentLevel + 1));
    })
  }
  return ret.join("\n");
}

console.log('Description:');
blocks.forEach(b => {
  console.log(describeBlock(b, 0));
});

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
