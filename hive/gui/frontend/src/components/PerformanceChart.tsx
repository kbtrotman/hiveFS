import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';

interface SeriesConfig {
  dataKey: string;
  color: string;
  label?: string;
}

interface PerformanceChartProps {
  title: string;
  description: string;
  dataKey?: string;
  color?: string;
  series?: SeriesConfig[];
  data?: ChartDatum[];
  isLoading?: boolean;
}

export interface ChartDatum {
  time: string;
  [key: string]: string | number;
}

const generatePlaceholderData = () => {
  const data = [];
  const now = new Date();
  for (let i = 6; i >= 0; i--) {
    const time = new Date(now.getTime() - i * 60 * 60 * 1000);
    data.push({
      time: `${time.getHours()}:00`,
      iops: Math.round(4000 + Math.random() * 1500),
      reads: Math.round(2500 + Math.random() * 800),
      writes: Math.round(1500 + Math.random() * 600),
      throughput: Math.round(200 + Math.random() * 100),
    });
  }
  return data;
};

export function PerformanceChart({
  title,
  description,
  dataKey,
  color,
  series,
  data,
  isLoading,
}: PerformanceChartProps) {
  const chartData = data && data.length > 0 ? data : generatePlaceholderData();
  const resolvedSeries: SeriesConfig[] =
    series && series.length > 0
      ? series
      : dataKey && color
        ? [{ dataKey, color }]
        : [];

  return (
    <Card>
      <CardHeader>
        <CardTitle>{title}</CardTitle>
        <CardDescription>{description}</CardDescription>
      </CardHeader>
      <CardContent>
        {isLoading && (
          <p className="text-sm text-muted-foreground mb-4">Loading latest metrics…</p>
        )}
        <ResponsiveContainer width="100%" height={200}>
          <LineChart data={chartData}>
            <CartesianGrid strokeDasharray="3 3" className="stroke-muted" />
            <XAxis 
              dataKey="time" 
              className="text-xs" 
              tick={{ fill: 'hsl(var(--muted-foreground))' }}
            />
            <YAxis 
              className="text-xs" 
              tick={{ fill: 'hsl(var(--muted-foreground))' }}
            />
            <Tooltip 
              contentStyle={{ 
                backgroundColor: 'hsl(var(--card))',
                border: '1px solid hsl(var(--border))',
                borderRadius: '6px',
              }}
            />
            {resolvedSeries.map((seriesConfig) => (
              <Line
                key={seriesConfig.dataKey}
                type="monotone"
                dataKey={seriesConfig.dataKey}
                stroke={seriesConfig.color}
                strokeWidth={2}
                dot={false}
                name={seriesConfig.label}
              />
            ))}
          </LineChart>
        </ResponsiveContainer>
      </CardContent>
    </Card>
  );
}
