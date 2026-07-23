import type { ButtonHTMLAttributes, ReactNode } from "react";
import { cn } from "@/lib/utils";

interface IconButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  children: ReactNode;
  variant?: "ghost" | "solid";
  size?: "md" | "lg";
}

export function IconButton({
  children,
  variant = "ghost",
  size = "md",
  className,
  ...rest
}: IconButtonProps) {
  return (
    <button
      className={cn(
        "inline-flex items-center justify-center rounded-full transition active:scale-95 disabled:pointer-events-none disabled:opacity-40",
        size === "lg" ? "h-14 w-14" : "h-10 w-10",
        variant === "solid"
          ? "bg-accent text-white shadow-glow hover:bg-accent-soft"
          : "bg-white/5 text-slate-200 hover:bg-white/10",
        className,
      )}
      {...rest}
    >
      {children}
    </button>
  );
}
