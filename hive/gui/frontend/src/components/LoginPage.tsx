import { useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card';
import { Input } from './ui/input';
import { Button } from './ui/button';
import { Label } from './ui/label';
import { ImageWithFallback } from './figma/ImageWithFallback';
import { Loader2 } from 'lucide-react';

interface LoginPageProps {
  onLoginSuccess: (token: string | null, user: any) => void;
}

// Put your API base here, or use an env var
const API_BASE =
  import.meta?.env?.VITE_API_BASE_URL ?? 'http://localhost:8000/api/v1/accounts';

export function LoginPage({ onLoginSuccess }: LoginPageProps) {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState('');

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    setIsLoading(true);
    setError('');

    try {
      const response = await fetch(`${API_BASE}/auth/login/`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        // if you switch to cookie/session auth later:
        // credentials: 'include',
        body: JSON.stringify({
          username, // or "email" if your backend expects email
          password,
        }),
      });

      if (!response.ok) {
        // Try to show a helpful error message from the API, if any
        let message = 'Invalid credentials';
        try {
          const errData = await response.json();
          if (errData.detail) message = errData.detail;
        } catch {
          /* ignore JSON parse errors */
        }
        throw new Error(message);
      }

      const data = await response.json();

      // dj-rest-auth token is usually in `key`
      const token =
        data.key || data.token || data.access || data.accessToken || null;

      // Option 1: if your login endpoint already returns user info:
      let user = data.user || null;

      // Option 2: if you want a fresh user profile from /auth/session/:
      if (!user) {
        try {
          const sessionRes = await fetch(`${API_BASE}/auth/session/`, {
            // if you use token auth:
            headers: token
              ? { Authorization: `Token ${token}` }
              : undefined,
            // if you use cookies instead:
            // credentials: 'include',
          });
          if (sessionRes.ok) {
            const sessionData = await sessionRes.json();
            user = sessionData.user ?? { username };
          }
        } catch {
          // fallback: at least pass username through
          user = { username };
        }
      }

      onLoginSuccess(token, user);
    } catch (err) {
      setError(
        err instanceof Error ? err.message : 'Login failed. Please try again.',
      );
    } finally {
      setIsLoading(false);
    }
  };

  // Demo login function (remove this in production)
  const handleDemoLogin = () => {
    setIsLoading(true);
    setTimeout(() => {
      onLoginSuccess('demo-token-123', { username: 'demo-user' });
      setIsLoading(false);
    }, 1000);
  };

  return (
    <div className="min-h-screen bg-background flex items-center justify-center p-4">
      <Card className="w-full max-w-md">
        <CardHeader className="space-y-4">
          <div className="flex justify-center">
            <ImageWithFallback 
              src="https://images.unsplash.com/photo-1568526381923-caf3fd520382?w=400&q=80"
              alt="HiveFS Logo"
              className="w-16 h-16 rounded-lg object-cover"
            />
          </div>
          <div className="text-center">
            <CardTitle>Welcome to HiveFS</CardTitle>
            <CardDescription>Sign in to access your filesystem dashboard</CardDescription>
          </div>
        </CardHeader>
        <CardContent>
          <form onSubmit={handleLogin} className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="username">Username</Label>
              <Input
                id="username"
                type="text"
                placeholder="Enter your username"
                value={username}
                onChange={(e) => setUsername(e.target.value)}
                disabled={isLoading}
                required
              />
            </div>

            <div className="space-y-2">
              <Label htmlFor="password">Password</Label>
              <Input
                id="password"
                type="password"
                placeholder="Enter your password"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                disabled={isLoading}
                required
              />
            </div>

            {error && (
              <div className="text-sm text-red-500 bg-red-50 dark:bg-red-950/20 p-3 rounded-md">
                {error}
              </div>
            )}

            <Button type="submit" className="w-full" disabled={isLoading}>
              {isLoading ? (
                <>
                  <Loader2 className="w-4 h-4 mr-2 animate-spin" />
                  Signing in...
                </>
              ) : (
                'Sign In'
              )}
            </Button>

            {/* Demo button - remove this in production */}
            <Button 
              type="button" 
              variant="outline" 
              className="w-full" 
              onClick={handleDemoLogin}
              disabled={isLoading}
            >
              Demo Login (Remove in Production)
            </Button>
          </form>

          <div className="mt-6 text-center text-sm text-muted-foreground">
            <p>Forgot your password? Contact your administrator.</p>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
