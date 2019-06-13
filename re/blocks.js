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
  if (blockType > 0xff) {
    console.log("Blocktype out of range, skipping");
    return;
  }
  const blockSize = isBigEndian ? file.readUInt32BE(pos + 3) : file.readUInt32LE(pos + 3);
  const contentType = isBigEndian ? file.readUInt16BE(pos + 7) : file.readUInt16LE(pos + 7);
  const totalBlockSize = 1 + 2 + 4 + blockSize;

  const blockTypeString = makeHexString(blockType);
  const contentTypeString = makeHexString(contentType);

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

const contentTypes = {
  "0x0030": {
    description: "product info, including version",
  },
  "0x1004": {
    description: "WAV list full",
  },
  "0x103a": {
    description: "WAV list",
  },
  "0x271a": {
    description: "only occurs in 12, contains 'Markers'",
  },
  "0x1028": {
    description: "session sample rate",
  },
  "0x100b": {
    description: "AUDIO region list (v5)",
  },
  "0x262a": {
    description: "AUDIO region list (v10)",
  },
  "0x1008": {
    description: "AUDIO region name, number (v5)",
  },
  "0x2629": {
    description: "AUDIO region name, number (v10)",
  },
  "0x1012": {
    description: "AUDIO region->track full map",
  },
  "0x1011": {
    description: "AUDIO region->track map entries",
  },
  "0x100f": {
    description: "AUDIO region->track entry",
  },
  "0x1015": {
    description: "AUDIO tracks",
  },
  "0x1014": {
    description: "AUDIO track name, number",
  },
  "0x1018": {
    description: "PLUGIN full list",
  },
  "0x1017": {
    description: "PLUGIN entry",
  },
  "0x1022": {
    description: "I/O input list",
  },
  "0x1021": {
    description: "I/O input entry",
  },
  "0x2000": {
    description: "MIDI events block",
  },
  "0x2002": {
    description: "MIDI regions map",
  },
  "0x2001": {
    description: "MIDI region name, number",
  },
  "0x1007": {
    description: "region name, number",
  },
  "0x1058": {
    description: "MIDI track information",
  },
  "0x2067": {
    description: "session info, path of session",
  },
  "0x2519": {
    description: "TRACK full list",
  },
  "0x251a": {
    description: "TRACK name, number",
  },
  "0x2511": {
    description: "Snaps block"
  }
  // "0x2056":
}



// idea is to create a nested schema
function describeBlock (block, indentLevel) {
  const contentType = contentTypes[block.contentTypeString];
  const description = contentType? contentType.description: "";
  const buffer = block.blockContent;
  const hexData = [];
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
    if (i > 0 && i % maxWidth === 0) {
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
