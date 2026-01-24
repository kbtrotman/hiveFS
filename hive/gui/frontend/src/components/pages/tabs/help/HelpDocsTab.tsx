import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Input } from '../../../ui/input';
import { BookOpen, MessageCircle, FileText, Video, ExternalLink, Search } from 'lucide-react';

const quickLinks = [
  { title: 'Getting Started Guide', description: 'Learn the basics of HiveFS', icon: BookOpen, link: '#' },
  { title: 'API Documentation', description: 'Complete API reference', icon: FileText, link: '#' },
  { title: 'Video Tutorials', description: 'Step-by-step video guides', icon: Video, link: '#' },
  { title: 'Community Forum', description: 'Get help from the community', icon: MessageCircle, link: '#' },
];

const commonTopics = [
  { title: 'How to configure storage pools', category: 'Storage', views: '1.2k' },
  { title: 'Setting up automated backups', category: 'Backup', views: '856' },
  { title: 'User permissions and roles', category: 'Security', views: '723' },
  { title: 'Monitoring system performance', category: 'Performance', views: '654' },
  { title: 'CLI command reference', category: 'CLI', views: '542' },
  { title: 'Troubleshooting connection issues', category: 'Troubleshooting', views: '489' },
];

const faqs = [
  { question: 'How do I add a new storage node?', answer: 'Navigate to Servers > Add Server and follow the setup wizard...' },
  { question: 'What are the system requirements?', answer: 'HiveFS requires at least 4GB RAM and 50GB storage...' },
  { question: 'How do I upgrade to the latest version?', answer: 'Go to Settings > System Updates and click Check for Updates...' },
  { question: 'Can I integrate HiveFS with other tools?', answer: 'Yes, HiveFS provides REST API and CLI tools for integration...' },
];

export function HelpDocsTab() {
  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Help & Documentation</h2>
          <p className="text-muted-foreground">Find answers and get support</p>
        </div>
        <Button>
          <MessageCircle className="w-4 h-4 mr-2" />
          Contact Support
        </Button>
      </div>

      <Card>
        <CardContent className="pt-6">
          <div className="flex gap-2">
            <div className="relative flex-1">
              <Search className="absolute left-3 top-1/2 transform -translate-y-1/2 w-5 h-5 text-muted-foreground" />
              <Input 
                placeholder="Search documentation, guides, and FAQs..." 
                className="pl-10"
              />
            </div>
            <Button>Search</Button>
          </div>
        </CardContent>
      </Card>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        {quickLinks.map((link, index) => {
          const Icon = link.icon;
          return (
            <Card key={index} className="cursor-pointer hover:bg-muted/50 transition-colors">
              <CardContent className="pt-6">
                <Icon className="w-8 h-8 text-primary mb-3" />
                <p className="mb-1">{link.title}</p>
                <p className="text-sm text-muted-foreground">{link.description}</p>
                <ExternalLink className="w-4 h-4 mt-3 text-muted-foreground" />
              </CardContent>
            </Card>
          );
        })}
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Popular Topics</CardTitle>
          <CardDescription>Most viewed help articles</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {commonTopics.map((topic, index) => (
              <div 
                key={index} 
                className="flex items-center justify-between py-3 border-b border-border last:border-0 cursor-pointer hover:bg-muted/50 transition-colors rounded px-2"
              >
                <div className="flex items-center gap-3">
                  <BookOpen className="w-5 h-5 text-muted-foreground" />
                  <div>
                    <p>{topic.title}</p>
                    <p className="text-sm text-muted-foreground">{topic.category}</p>
                  </div>
                </div>
                <span className="text-sm text-muted-foreground">{topic.views} views</span>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Frequently Asked Questions</CardTitle>
          <CardDescription>Quick answers to common questions</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          {faqs.map((faq, index) => (
            <div key={index} className="space-y-2 pb-4 border-b border-border last:border-0">
              <p>{faq.question}</p>
              <p className="text-sm text-muted-foreground">{faq.answer}</p>
              <Button variant="link" className="p-0 h-auto">Read more →</Button>
            </div>
          ))}
        </CardContent>
      </Card>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card>
          <CardHeader>
            <CardTitle>System Information</CardTitle>
            <CardDescription>Current installation details</CardDescription>
          </CardHeader>
          <CardContent className="space-y-2">
            <div className="flex justify-between">
              <span className="text-muted-foreground">HiveFS Version</span>
              <span>3.2.0</span>
            </div>
            <div className="flex justify-between">
              <span className="text-muted-foreground">License Type</span>
              <span>Enterprise</span>
            </div>
            <div className="flex justify-between">
              <span className="text-muted-foreground">License Expires</span>
              <span>2025-12-31</span>
            </div>
            <div className="flex justify-between">
              <span className="text-muted-foreground">Support Level</span>
              <span>Premium</span>
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>Support Resources</CardTitle>
            <CardDescription>Additional ways to get help</CardDescription>
          </CardHeader>
          <CardContent className="space-y-3">
            <Button variant="outline" className="w-full justify-start">
              <MessageCircle className="w-4 h-4 mr-2" />
              Live Chat Support
            </Button>
            <Button variant="outline" className="w-full justify-start">
              <FileText className="w-4 h-4 mr-2" />
              Submit a Ticket
            </Button>
            <Button variant="outline" className="w-full justify-start">
              <ExternalLink className="w-4 h-4 mr-2" />
              Visit Knowledge Base
            </Button>
            <Button variant="outline" className="w-full justify-start">
              <Video className="w-4 h-4 mr-2" />
              Schedule Training
            </Button>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
