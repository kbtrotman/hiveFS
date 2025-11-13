// next.config.mjs

/** @type {import('next').NextConfig} */
const nextConfig = {
  experimental: { esmExternals: 'loose' },
  trailingSlash: false,

  async redirects() {
    return [];
  },

  transpilePackages: [
    'antd',
    '@ant-design',
    '@ant-design/icons',
    'rc-util',
    'rc-tree',
    'rc-motion',
    'rc-virtual-list',
    'rc-resize-observer',
    'rc-picker',
  ],

  webpack: (config) => {
    config.resolve.alias['rc-util/es'] = 'rc-util/lib';
    config.resolve.alias['rc-tree/es'] = 'rc-tree/lib';
    config.resolve.alias['rc-motion/es'] = 'rc-motion/lib';
    config.resolve.alias['rc-virtual-list/es'] = 'rc-virtual-list/lib';
    config.resolve.alias['rc-resize-observer/es'] = 'rc-resize-observer/lib';
    config.resolve.alias['rc-picker/es'] = 'rc-picker/lib';  // <-- add this
    return config;
  },
};

export default nextConfig;
