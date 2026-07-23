/** @type {import('tailwindcss').Config} */
export default {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        base: {
          950: "#0a0c10",
          900: "#0f131a",
          850: "#141922",
          800: "#1b212c",
          700: "#28303d",
          600: "#3a4557",
        },
        accent: {
          DEFAULT: "#f97316",
          soft: "#fb923c",
          deep: "#c2410c",
        },
        signal: "#34d399",
      },
      fontFamily: {
        sans: ["Inter", "system-ui", "sans-serif"],
      },
      boxShadow: {
        panel: "0 16px 50px -12px rgba(0,0,0,0.55)",
        glow: "0 0 40px -8px rgba(249,115,22,0.45)",
      },
      keyframes: {
        "fade-up": {
          "0%": { opacity: "0", transform: "translateY(10px)" },
          "100%": { opacity: "1", transform: "translateY(0)" },
        },
        "pulse-ring": {
          "0%,100%": { opacity: "0.35" },
          "50%": { opacity: "1" },
        },
      },
      animation: {
        "fade-up": "fade-up 320ms ease-out",
        "pulse-ring": "pulse-ring 1.6s ease-in-out infinite",
      },
    },
  },
  plugins: [],
};
