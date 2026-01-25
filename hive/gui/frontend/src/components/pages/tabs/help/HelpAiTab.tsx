import { useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Button } from '../../../ui/button';
import { ScrollArea } from '../../../ui/scroll-area';
import { Separator } from '../../../ui/separator';
import { Send, Sparkles } from 'lucide-react';

interface Message {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  timestamp: Date;
}

export function HelpAiTab() {
  const [messages, setMessages] = useState<Message[]>([
    {
      id: '1',
      role: 'assistant',
      content: "Hi! I'm Buzz Flowers, but you can just call me Buzz—your HiveFS assistant. I can help you search documentation, answer questions about your cluster, and configure settings and notifications. What can I help you with today?",
      timestamp: new Date(),
    },
  ]);
  const [inputValue, setInputValue] = useState('');

  const handleSendMessage = () => {
    if (!inputValue.trim()) return;

    const userMessage: Message = {
      id: Date.now().toString(),
      role: 'user',
      content: inputValue,
      timestamp: new Date(),
    };

    setMessages((prev) => [...prev, userMessage]);
    setInputValue('');

    // Simulate AI response
    setTimeout(() => {
      const assistantMessage: Message = {
        id: (Date.now() + 1).toString(),
        role: 'assistant',
        content: "I'm here to help! This is a demo response. In production, I would search the HiveFS documentation and provide helpful answers about cluster management, node configuration, and system settings.",
        timestamp: new Date(),
      };
      setMessages((prev) => [...prev, assistantMessage]);
    }, 1000);
  };

  const handleKeyPress = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') {
      handleSendMessage();
    }
  };

  const formatTime = (date: Date) => {
    return date.toLocaleTimeString('en-US', {
      hour: 'numeric',
      minute: '2-digit',
      hour12: true,
    });
  };

  return (
    <div className="flex h-full w-full items-center justify-center bg-gradient-to-br from-background via-primary/5 to-background p-8">
      <Card className="flex h-[700px] w-full max-w-3xl flex-col border-primary/20 bg-gradient-to-b from-background/80 to-background shadow-2xl">
        <CardHeader className="pb-4">
          <div className="flex items-center gap-3">
            <div className="flex h-12 w-12 items-center justify-center rounded-full bg-gradient-to-br from-primary/20 to-primary/40">
              <Sparkles className="h-5 w-5 text-primary-foreground" />
            </div>
            <div>
              <CardTitle className="text-foreground/90">Buzz</CardTitle>
              <p className="text-muted-foreground text-xs mt-0.5">
                HiveFS Support Assistant
              </p>
            </div>
          </div>
        </CardHeader>

        <Separator />

        <CardContent className="flex-1 flex flex-col p-0 overflow-hidden">
          {/* Messages Area */}
          <ScrollArea className="flex-1 px-6 py-4">
            <div className="space-y-4">
              {messages.map((message) => (
                <div
                  key={message.id}
                  className={`flex ${
                    message.role === 'user' ? 'justify-end' : 'justify-start'
                  }`}
                >
                  <div
                    className={`max-w-[80%] rounded-2xl px-4 py-2.5 ${
                      message.role === 'user'
                        ? 'bg-gradient-to-r from-primary to-primary/80 text-primary-foreground shadow-lg'
                        : 'bg-gradient-to-r from-muted to-muted/70 text-foreground'
                    }`}
                  >
                    <p className="text-sm leading-relaxed">{message.content}</p>
                    <p
                      className={`text-xs mt-1.5 ${
                        message.role === 'user'
                          ? 'text-primary-foreground/60'
                          : 'text-muted-foreground'
                      }`}
                    >
                      {formatTime(message.timestamp)}
                    </p>
                  </div>
                </div>
              ))}
            </div>
          </ScrollArea>

          <Separator />

          {/* Input Area */}
          <div className="p-6 bg-gradient-to-b from-background/60 to-background">
            {/* Quick Actions */}
            <div className="flex gap-2 mb-3 flex-wrap">
              <Button
                variant="outline"
                size="sm"
                className="text-xs h-7"
                onClick={() =>
                  setInputValue('How do I add a new node to my cluster?')
                }
              >
                Add New Node
              </Button>
              <Button
                variant="outline"
                size="sm"
                className="text-xs h-7"
                onClick={() =>
                  setInputValue('Show me cluster health documentation')
                }
              >
                Cluster Health
              </Button>
              <Button
                variant="outline"
                size="sm"
                className="text-xs h-7"
                onClick={() =>
                  setInputValue('Help me set up email notifications')
                }
              >
                Setup Notifications
              </Button>
            </div>

            {/* Input Field */}
            <div className="flex gap-2">
              <Input
                placeholder="Ask Buzz anything about HiveFS..."
                value={inputValue}
                onChange={(e) => setInputValue(e.target.value)}
                onKeyPress={handleKeyPress}
                className="text-sm flex-1"
              />
              <Button
                onClick={handleSendMessage}
                disabled={!inputValue.trim()}
                className="px-4"
              >
                <Send className="w-4 h-4" />
              </Button>
            </div>

            <p
              className="text-muted-foreground opacity-60 mt-2"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              Buzz can search documentation, answer questions, and help configure your HiveFS cluster.
            </p>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
