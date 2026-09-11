const fs = require("fs");
const path = require("path");
const sharp = require("sharp");

const root = path.resolve(__dirname, "..");
const sourceDir = "D:/下载";
const outputDir = path.join(root, "main", "UI", "asset");
const icons = [
  { source: "姿态仪.svg", name: "icon_imu" },
  { source: "下载.svg", name: "icon_download" },
  { source: "环境.svg", name: "icon_environment" },
  { source: "WIFI.svg", name: "icon_wifi" },
];

function cArray(bytes) {
  const lines = [];
  for (let index = 0; index < bytes.length; index += 12) {
    lines.push("    " + Array.from(bytes.subarray(index, index + 12),
        (value) => `0x${value.toString(16).padStart(2, "0")}`).join(", "));
  }
  return lines.join(",\n");
}

function cSource(name, pixels) {
  return `#include "lvgl.h"

const uint8_t ${name}_map[] = {
${cArray(pixels)}
};

const lv_image_dsc_t ${name} = {
    .header.cf = LV_COLOR_FORMAT_ARGB8888,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = 40,
    .header.h = 40,
    .data_size = sizeof(${name}_map),
    .data = ${name}_map,
};
`;
}

function header(name) {
  return `#ifndef ${name.toUpperCase()}_H
#define ${name.toUpperCase()}_H

#include "lvgl.h"

extern const lv_image_dsc_t ${name};

#endif
`;
}

async function main() {
  fs.mkdirSync(outputDir, { recursive: true });
  for (const icon of icons) {
    const { data, info } = await sharp(path.join(sourceDir, icon.source), { density: 192 })
      .resize(40, 40, { fit: "contain", background: { r: 0, g: 0, b: 0, alpha: 0 } })
      .ensureAlpha()
      .raw()
      .toBuffer({ resolveWithObject: true });
    if (info.width !== 40 || info.height !== 40 || info.channels !== 4) {
      throw new Error(`Unexpected ${icon.name} output: ${JSON.stringify(info)}`);
    }
    fs.writeFileSync(path.join(outputDir, `${icon.name}.c`), cSource(icon.name, data));
    fs.writeFileSync(path.join(outputDir, `${icon.name}.h`), header(icon.name));
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
