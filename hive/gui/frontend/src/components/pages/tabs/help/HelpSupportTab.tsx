import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Badge } from '../../../ui/badge';
import { Separator } from '../../../ui/separator';
import {
  MessageCircle,
  Mail,
  Github,
  Book,
  Bug,
  Lightbulb,
  Users,
  ExternalLink,
  AlertCircle,
  FileText,
  MessageSquare,
  Code,
  HelpCircle,
  Clock,
  CheckCircle,
} from 'lucide-react';

export function HelpSupportTab() {
  const handleOpenGithub = (section: string = '') => {
    const baseUrl = 'https://github.com/kbtrotman/hivefs';
    const url = section ? `${baseUrl}/${section}` : baseUrl;
    window.open(url, '_blank');
  };

  const handleEmailSupport = (subject: string = 'HiveFS Support Request') => {
    window.location.href = `mailto:kbtrotman@gmail.com?subject=${encodeURIComponent(subject)}`;
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">Get Support</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Connect with the HiveFS community and get help with your cluster
        </p>
      </div>

      {/* Welcome Banner */}
      <div className="bg-gradient-to-r from-blue-500/10 to-purple-500/10 border border-blue-500/30 rounded-lg p-6">
        <div className="flex items-start gap-4">
          <div className="bg-blue-500/20 rounded-lg p-3">
            <MessageCircle className="w-8 h-8 text-blue-500" />
          </div>
          <div className="flex-1">
            <h2 className="text-lg font-semibold text-foreground/90 mb-2">
              Welcome to HiveFS Support
            </h2>
            <p className="text-sm text-muted-foreground mb-3">
              HiveFS is an open-source distributed filesystem project. Our community-driven support
              channels are here to help you succeed. Whether you're reporting a bug, requesting a
              feature, or need technical assistance, we're committed to supporting you.
            </p>
            <div className="flex items-center gap-4 text-xs text-muted-foreground">
              <div className="flex items-center gap-1">
                <Users className="w-4 h-4" />
                <span>Community Supported</span>
              </div>
              <span>•</span>
              <div className="flex items-center gap-1">
                <Code className="w-4 h-4" />
                <span>Open Source</span>
              </div>
              <span>•</span>
              <div className="flex items-center gap-1">
                <Github className="w-4 h-4" />
                <span>GitHub Hosted</span>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Primary Support Channels */}
      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        {/* GitHub Support */}
        <Card className="border-primary/20 bg-gradient-to-b from-background/90 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <div className="flex items-center justify-between">
              <CardTitle className="text-foreground/90 flex items-center gap-2">
                <Github className="w-5 h-5" />
                GitHub Community
              </CardTitle>
              <Badge variant="outline" className="text-xs">Primary Channel</Badge>
            </div>
          </CardHeader>
          <CardContent className="space-y-4">
            <p className="text-sm text-muted-foreground">
              Our GitHub repository is the central hub for HiveFS development, issues, and
              community discussions. Connect with other users and contributors.
            </p>

            <Separator />

            <div className="space-y-3">
              {/* Issues */}
              <div className="bg-muted/30 rounded-lg p-3 border border-border">
                <div className="flex items-start gap-3">
                  <Bug className="w-5 h-5 text-red-500 mt-0.5" />
                  <div className="flex-1">
                    <p className="text-sm font-medium mb-1">Report Issues</p>
                    <p className="text-xs text-muted-foreground mb-2">
                      Found a bug or technical problem? Create an issue to get help from the
                      community.
                    </p>
                    <Button
                      size="sm"
                      variant="outline"
                      className="text-xs h-7"
                      onClick={() => handleOpenGithub('issues/new')}
                    >
                      <Bug className="w-3 h-3 mr-1" />
                      Report Bug
                      <ExternalLink className="w-3 h-3 ml-1" />
                    </Button>
                  </div>
                </div>
              </div>

              {/* Feature Requests */}
              <div className="bg-muted/30 rounded-lg p-3 border border-border">
                <div className="flex items-start gap-3">
                  <Lightbulb className="w-5 h-5 text-yellow-500 mt-0.5" />
                  <div className="flex-1">
                    <p className="text-sm font-medium mb-1">Feature Requests</p>
                    <p className="text-xs text-muted-foreground mb-2">
                      Have an idea for a new feature? Share your suggestions with the community.
                    </p>
                    <Button
                      size="sm"
                      variant="outline"
                      className="text-xs h-7"
                      onClick={() => handleOpenGithub('issues/new')}
                    >
                      <Lightbulb className="w-3 h-3 mr-1" />
                      Request Feature
                      <ExternalLink className="w-3 h-3 ml-1" />
                    </Button>
                  </div>
                </div>
              </div>

              {/* Discussions */}
              <div className="bg-muted/30 rounded-lg p-3 border border-border">
                <div className="flex items-start gap-3">
                  <MessageSquare className="w-5 h-5 text-blue-500 mt-0.5" />
                  <div className="flex-1">
                    <p className="text-sm font-medium mb-1">Community Discussions</p>
                    <p className="text-xs text-muted-foreground mb-2">
                      Ask questions, share experiences, and engage with other HiveFS users.
                    </p>
                    <Button
                      size="sm"
                      variant="outline"
                      className="text-xs h-7"
                      onClick={() => handleOpenGithub('discussions')}
                    >
                      <MessageSquare className="w-3 h-3 mr-1" />
                      Join Discussion
                      <ExternalLink className="w-3 h-3 ml-1" />
                    </Button>
                  </div>
                </div>
              </div>
            </div>

            <Separator />

            <Button className="w-full" onClick={() => handleOpenGithub()}>
              <Github className="w-4 h-4 mr-2" />
              Visit GitHub Repository
              <ExternalLink className="w-4 h-4 ml-2" />
            </Button>
          </CardContent>
        </Card>

        {/* Direct Contact */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Mail className="w-5 h-5" />
              Direct Contact
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <p className="text-sm text-muted-foreground">
              For private inquiries, security concerns, or matters that require direct
              communication with the maintainer.
            </p>

            <Separator />

            <div className="bg-muted/30 rounded-lg p-4 border border-border">
              <div className="flex items-start gap-3 mb-3">
                <Mail className="w-5 h-5 text-blue-500 mt-0.5" />
                <div className="flex-1">
                  <p className="text-sm font-medium mb-1">Email Support</p>
                  <p className="text-xs text-muted-foreground mb-2">
                    Reach out directly to the project maintainer
                  </p>
                  <div className="bg-background rounded p-2 border border-border">
                    <code className="text-xs text-primary">kbtrotman@gmail.com</code>
                  </div>
                </div>
              </div>
            </div>

            <div className="space-y-2">
              <p className="text-xs font-medium text-foreground/90">Best for:</p>
              <div className="space-y-1.5">
                <div className="flex items-start gap-2 text-xs text-muted-foreground">
                  <CheckCircle className="w-3 h-3 text-green-500 mt-0.5 flex-shrink-0" />
                  <span>Security vulnerability reports</span>
                </div>
                <div className="flex items-start gap-2 text-xs text-muted-foreground">
                  <CheckCircle className="w-3 h-3 text-green-500 mt-0.5 flex-shrink-0" />
                  <span>Private or sensitive matters</span>
                </div>
                <div className="flex items-start gap-2 text-xs text-muted-foreground">
                  <CheckCircle className="w-3 h-3 text-green-500 mt-0.5 flex-shrink-0" />
                  <span>Partnership or collaboration inquiries</span>
                </div>
                <div className="flex items-start gap-2 text-xs text-muted-foreground">
                  <CheckCircle className="w-3 h-3 text-green-500 mt-0.5 flex-shrink-0" />
                  <span>Media or press contacts</span>
                </div>
              </div>
            </div>

            <Separator />

            <div className="space-y-2">
              <Button
                className="w-full"
                onClick={() => handleEmailSupport('HiveFS Support Request')}
              >
                <Mail className="w-4 h-4 mr-2" />
                Send Email
              </Button>
              <Button
                variant="outline"
                className="w-full"
                onClick={() => handleEmailSupport('HiveFS Security Issue - CONFIDENTIAL')}
              >
                <AlertCircle className="w-4 h-4 mr-2" />
                Report Security Issue
              </Button>
            </div>

            <div className="bg-yellow-500/10 border border-yellow-500/30 rounded p-3 mt-4">
              <div className="flex items-start gap-2">
                <Clock className="w-4 h-4 text-yellow-600 mt-0.5 flex-shrink-0" />
                <p className="text-xs text-muted-foreground">
                  <strong>Response Time:</strong> Please allow 2-3 business days for email
                  responses. For faster community support, consider using GitHub Discussions.
                </p>
              </div>
            </div>
          </CardContent>
        </Card>
      </div>

      {/* Resources & Documentation */}
      <Card className="shadow-lg">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Book className="w-5 h-5" />
            Resources & Documentation
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-3 gap-4">
            {/* Documentation */}
            <div className="bg-muted/30 rounded-lg p-4 border border-border">
              <div className="flex items-start gap-3">
                <FileText className="w-5 h-5 text-blue-500" />
                <div>
                  <p className="text-sm font-medium mb-1">Documentation</p>
                  <p className="text-xs text-muted-foreground mb-3">
                    Comprehensive guides, API references, and setup instructions
                  </p>
                  <Button
                    size="sm"
                    variant="outline"
                    className="text-xs h-7"
                    onClick={() => handleOpenGithub('wiki')}
                  >
                    <Book className="w-3 h-3 mr-1" />
                    View Docs
                    <ExternalLink className="w-3 h-3 ml-1" />
                  </Button>
                </div>
              </div>
            </div>

            {/* README */}
            <div className="bg-muted/30 rounded-lg p-4 border border-border">
              <div className="flex items-start gap-3">
                <FileText className="w-5 h-5 text-green-500" />
                <div>
                  <p className="text-sm font-medium mb-1">README</p>
                  <p className="text-xs text-muted-foreground mb-3">
                    Quick start guide, installation, and basic usage
                  </p>
                  <Button
                    size="sm"
                    variant="outline"
                    className="text-xs h-7"
                    onClick={() => handleOpenGithub()}
                  >
                    <Github className="w-3 h-3 mr-1" />
                    View README
                    <ExternalLink className="w-3 h-3 ml-1" />
                  </Button>
                </div>
              </div>
            </div>

            {/* Contributing */}
            <div className="bg-muted/30 rounded-lg p-4 border border-border">
              <div className="flex items-start gap-3">
                <Users className="w-5 h-5 text-purple-500" />
                <div>
                  <p className="text-sm font-medium mb-1">Contributing</p>
                  <p className="text-xs text-muted-foreground mb-3">
                    Guidelines for contributing code, documentation, or bug fixes
                  </p>
                  <Button
                    size="sm"
                    variant="outline"
                    className="text-xs h-7"
                    onClick={() => handleOpenGithub('blob/main/CONTRIBUTING.md')}
                  >
                    <Code className="w-3 h-3 mr-1" />
                    Contribute
                    <ExternalLink className="w-3 h-3 ml-1" />
                  </Button>
                </div>
              </div>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Getting the Best Support */}
      <div className="grid grid-cols-2 gap-6">
        <Card className="shadow-lg">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <HelpCircle className="w-5 h-5" />
              Getting the Best Support
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-3">
            <p className="text-sm text-muted-foreground">
              Help us help you faster by providing detailed information:
            </p>

            <div className="space-y-2">
              <div className="flex items-start gap-2 text-sm">
                <CheckCircle className="w-4 h-4 text-green-500 mt-0.5 flex-shrink-0" />
                <div>
                  <p className="font-medium text-foreground/90">HiveFS version</p>
                  <p className="text-xs text-muted-foreground">
                    Include your version number and build information
                  </p>
                </div>
              </div>
              <div className="flex items-start gap-2 text-sm">
                <CheckCircle className="w-4 h-4 text-green-500 mt-0.5 flex-shrink-0" />
                <div>
                  <p className="font-medium text-foreground/90">Operating system & environment</p>
                  <p className="text-xs text-muted-foreground">
                    OS version, hardware specs, cluster configuration
                  </p>
                </div>
              </div>
              <div className="flex items-start gap-2 text-sm">
                <CheckCircle className="w-4 h-4 text-green-500 mt-0.5 flex-shrink-0" />
                <div>
                  <p className="font-medium text-foreground/90">Steps to reproduce</p>
                  <p className="text-xs text-muted-foreground">
                    Clear, numbered steps that reliably reproduce the issue
                  </p>
                </div>
              </div>
              <div className="flex items-start gap-2 text-sm">
                <CheckCircle className="w-4 h-4 text-green-500 mt-0.5 flex-shrink-0" />
                <div>
                  <p className="font-medium text-foreground/90">Error messages or logs</p>
                  <p className="text-xs text-muted-foreground">
                    Include relevant error messages, stack traces, or log excerpts
                  </p>
                </div>
              </div>
              <div className="flex items-start gap-2 text-sm">
                <CheckCircle className="w-4 h-4 text-green-500 mt-0.5 flex-shrink-0" />
                <div>
                  <p className="font-medium text-foreground/90">Expected vs. actual behavior</p>
                  <p className="text-xs text-muted-foreground">
                    Describe what you expected and what actually happened
                  </p>
                </div>
              </div>
            </div>
          </CardContent>
        </Card>

        <Card className="shadow-lg">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <AlertCircle className="w-5 h-5" />
              Support Guidelines
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-3">
            <div className="bg-blue-500/10 border border-blue-500/30 rounded p-3">
              <div className="flex items-start gap-2">
                <MessageCircle className="w-4 h-4 text-blue-500 mt-0.5 flex-shrink-0" />
                <div>
                  <p className="text-sm font-medium text-blue-700 dark:text-blue-400 mb-1">
                    Public Issues First
                  </p>
                  <p className="text-xs text-muted-foreground">
                    Please use GitHub Issues or Discussions for general support questions. This
                    helps build a knowledge base that benefits the entire community.
                  </p>
                </div>
              </div>
            </div>

            <div className="bg-yellow-500/10 border border-yellow-500/30 rounded p-3">
              <div className="flex items-start gap-2">
                <AlertCircle className="w-4 h-4 text-yellow-600 mt-0.5 flex-shrink-0" />
                <div>
                  <p className="text-sm font-medium text-yellow-700 dark:text-yellow-400 mb-1">
                    Security Issues
                  </p>
                  <p className="text-xs text-muted-foreground">
                    For security vulnerabilities, please email directly rather than creating a
                    public issue. Include "SECURITY" in the subject line.
                  </p>
                </div>
              </div>
            </div>

            <div className="bg-green-500/10 border border-green-500/30 rounded p-3">
              <div className="flex items-start gap-2">
                <Users className="w-4 h-4 text-green-600 mt-0.5 flex-shrink-0" />
                <div>
                  <p className="text-sm font-medium text-green-700 dark:text-green-400 mb-1">
                    Community Contributions
                  </p>
                  <p className="text-xs text-muted-foreground">
                    HiveFS is community-driven. Consider contributing fixes, documentation, or
                    helping other users if you've solved similar problems.
                  </p>
                </div>
              </div>
            </div>

            <div className="bg-purple-500/10 border border-purple-500/30 rounded p-3">
              <div className="flex items-start gap-2">
                <Code className="w-4 h-4 text-purple-600 mt-0.5 flex-shrink-0" />
                <div>
                  <p className="text-sm font-medium text-purple-700 dark:text-purple-400 mb-1">
                    Search First
                  </p>
                  <p className="text-xs text-muted-foreground">
                    Before creating a new issue, search existing issues and discussions. Your
                    question may already be answered.
                  </p>
                </div>
              </div>
            </div>
          </CardContent>
        </Card>
      </div>

      {/* Footer Note */}
      <div className="bg-muted/30 border border-border rounded-lg p-4 text-center">
        <p className="text-sm text-muted-foreground">
          <strong>Thank you for using HiveFS!</strong> As an open-source project, we rely on
          community support and contributions. Your feedback helps make HiveFS better for everyone.
        </p>
        <div className="flex items-center justify-center gap-2 mt-2">
          <Badge variant="outline" className="text-xs">
            <Github className="w-3 h-3 mr-1" />
            Open Source
          </Badge>
          <Badge variant="outline" className="text-xs">
            <Users className="w-3 h-3 mr-1" />
            Community Driven
          </Badge>
          <Badge variant="outline" className="text-xs">
            <Code className="w-3 h-3 mr-1" />
            Always Improving
          </Badge>
        </div>
      </div>
    </div>
  );
}
