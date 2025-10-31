import * as React from 'react';
import { PlasmicCanvasHost } from '@plasmicapp/loader-nextjs';
import { PLASMIC } from '@/plasmic-init';
import { BrowserRouter as Router, Routes, Route } from 'react-router-dom';

export default function PlasmicHost() {
  return PLASMIC && <PlasmicCanvasHost />;
}


function AppRoot() {
  return (
    <Router>
      <Routes>
        <Route path="/hive-setup" element={<PlasmicCanvasHost />} />
        <Route path="/hive-mon" element={<PlasmicCanvasHost />} />
        {/* Other routes for your application */}
      </Routes>
    </Router>
  );
}