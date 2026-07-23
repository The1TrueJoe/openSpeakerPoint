import type { ReactNode } from "react";
import { cn } from "@/lib/utils";

interface CardProps {
  title?: string;
  icon?: ReactNode;
  action?: ReactNode;
  className?: string;
  children: ReactNode;
}

export function Card({ title, icon, action, className, children }: CardProps) {
  return (
    <section className={cn("card animate-fade-up", className)}>
      {(title || action) && (
        <header className="mb-4 flex items-center justify-between">
          <div className="flex items-center gap-2 text-sm font-semibold uppercase tracking-wide text-slate-400">
            {icon}
            {title}
          </div>
          {action}
        </header>
      )}
      {children}
    </section>
  );
}
