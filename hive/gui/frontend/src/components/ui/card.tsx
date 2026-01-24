import * as React from "react";

import { cn } from "./utils";

function Card({
  className,
  children,
  style,
  ...props
}: React.ComponentProps<"div">) {
  const baseColor = "var(--card)";
  const gradientStyle: React.CSSProperties = {
    backgroundColor: baseColor,
    backgroundImage: `
      radial-gradient(circle at 20% 20%, rgba(59, 130, 246, 0.09), transparent 58%),
      radial-gradient(circle at 80% 82%, rgba(188, 126, 83, 0.12), transparent 35%),
      linear-gradient(
        135deg,
        color-mix(in srgb, ${baseColor} 92%, rgba(59, 130, 246, 0.14)),
        color-mix(in srgb, ${baseColor} 92%, rgba(188, 126, 83, 0.14))
      )
    `,
  };

  return (
    <div
      data-slot="card"
      className={cn(
        "relative overflow-hidden rounded-xl border border-white/10 text-card-foreground shadow-[0_25px_45px_-30px_rgba(15,23,42,0.9)]",
        className,
      )}
      style={{ ...gradientStyle, ...style }}
      {...props}
    >
      <div
        aria-hidden="true"
        className="pointer-events-none absolute -inset-16 bg-[radial-gradient(circle_at_20%_20%,rgba(59,130,246,0.08),transparent_58%)] blur-3xl opacity-35"
      />
      <div
        aria-hidden="true"
        className="pointer-events-none absolute -inset-24 bg-[radial-gradient(circle at 80% 82%, rgba(188, 126, 83, 0.12), transparent 35%)] blur-3xl opacity-35"
      />
      <div
        aria-hidden="true"
        className="pointer-events-none absolute inset-0 bg-gradient-to-br from-sky-500/10 via-transparent to-orange-500/10 opacity-50"
      />
      <div
        aria-hidden="true"
        className="pointer-events-none absolute inset-px rounded-[inherit] border border-white/5"
      />
      <div className="relative flex flex-col gap-6">{children}</div>
    </div>
  );
}

function CardHeader({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div
      data-slot="card-header"
      className={cn(
        "@container/card-header grid auto-rows-min grid-rows-[auto_auto] items-start gap-1.5 px-6 pt-6 has-data-[slot=card-action]:grid-cols-[1fr_auto] [.border-b]:pb-6",
        className,
      )}
      {...props}
    />
  );
}

function CardTitle({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <h4
      data-slot="card-title"
      className={cn("leading-none", className)}
      {...props}
    />
  );
}

function CardDescription({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <p
      data-slot="card-description"
      className={cn("text-muted-foreground", className)}
      {...props}
    />
  );
}

function CardAction({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div
      data-slot="card-action"
      className={cn(
        "col-start-2 row-span-2 row-start-1 self-start justify-self-end",
        className,
      )}
      {...props}
    />
  );
}

function CardContent({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div
      data-slot="card-content"
      className={cn("px-6 [&:last-child]:pb-6", className)}
      {...props}
    />
  );
}

function CardFooter({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div
      data-slot="card-footer"
      className={cn("flex items-center px-6 pb-6 [.border-t]:pt-6", className)}
      {...props}
    />
  );
}

export {
  Card,
  CardHeader,
  CardFooter,
  CardTitle,
  CardAction,
  CardDescription,
  CardContent,
};
