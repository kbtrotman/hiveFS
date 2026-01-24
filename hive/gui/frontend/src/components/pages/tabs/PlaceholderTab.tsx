export interface PlaceholderTabProps {
  title: string;
  description?: string;
}

export function PlaceholderTab({ title, description }: PlaceholderTabProps) {
  return (
    <div className="rounded-xl border border-dashed border-border bg-background/60 p-8 text-sm text-muted-foreground">
      <p className="text-lg font-semibold text-foreground">{title}</p>
      <p className="mt-2">
        {description ?? 'This view is not wired up yet. Content will appear here soon.'}
      </p>
    </div>
  );
}
