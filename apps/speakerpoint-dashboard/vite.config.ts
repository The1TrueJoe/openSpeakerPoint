import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { fileURLToPath, URL } from "node:url";

// The dashboard is served as static files by BusyBox httpd on the device.
// Emit relative asset paths and a compact bundle sized for the 16 MB flash.
export default defineConfig({
  plugins: [react()],
  base: "./",
  resolve: {
    alias: {
      "@": fileURLToPath(new URL("./src", import.meta.url)),
    },
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2020",
    cssMinify: true,
    reportCompressedSize: false,
  },
});
