import type { ReactNode } from "react";
export function Panel({
  title,
  description,
  children,
  actions,
  className = "",
}: {
  title: string;
  description?: string;
  children: ReactNode;
  actions?: ReactNode;
  className?: string;
}) {
  return (
    <section className={"panel " + className}>
      <header className="panel-heading">
        <div>
          <h2>{title}</h2>
          {description && <p>{description}</p>}
        </div>
        {actions && <div className="panel-actions">{actions}</div>}
      </header>
      {children}
    </section>
  );
}
export function Field({
  label,
  children,
  hint,
}: {
  label: string;
  children: ReactNode;
  hint?: string;
}) {
  return (
    <label className="field">
      <span>{label}</span>
      {children}
      {hint && <small>{hint}</small>}
    </label>
  );
}
export function NumberInput({
  value,
  onChange,
  min = 0,
  max = 1000,
  step = 1,
  disabled = false,
}: {
  value: number;
  onChange: (n: number) => void;
  min?: number;
  max?: number;
  step?: number;
  disabled?: boolean;
}) {
  return (
    <input
      type="number"
      value={value}
      min={min}
      max={max}
      step={step}
      disabled={disabled}
      onWheel={(e) => e.currentTarget.blur()}
      onChange={(e) => {
        const v = e.currentTarget.valueAsNumber;
        if (Number.isFinite(v)) onChange(v);
      }}
    />
  );
}
export function Badge({
  children,
  tone = "neutral",
}: {
  children: ReactNode;
  tone?: string;
}) {
  return <span className={"badge " + tone}>{children}</span>;
}
export function Empty({
  title,
  description,
}: {
  title: string;
  description: string;
}) {
  return (
    <div className="empty">
      <div className="empty-orbit">◌</div>
      <strong>{title}</strong>
      <p>{description}</p>
    </div>
  );
}
export function Alert({ children }: { children: ReactNode }) {
  return <div className="alert">{children}</div>;
}
export function errorMessage(e: unknown) {
  return e instanceof Error ? e.message : String(e);
}
