import { initPlasmicLoader } from "@plasmicapp/loader-nextjs";
import { PlasmicRootProvider, PlasmicComponent } from '@plasmicapp/loader-react';
import { PLASMIC } from '../plasmic-init';

export const PLASMIC = initPlasmicLoader({
  projects: [
    {
      id: "c8gT28iq7v39gxBJ6CcbGP",
      token: "vgOBIsGV4sdbGDBdw0krBZbdIysWmwJvtCetA133bMT6cArukHVSobrFAD6rmQeTnlcSGbuX77IF1j7GWA",
    },
  ],

  // By default Plasmic will use the last published version of your project.
  // For development, you can set preview to true, which will use the unpublished
  // project, allowing you to see your designs without publishing.  Please
  // only use this for development, as this is significantly slower.
  preview: false,
});


export function MyComponent() {
  return (
    <PlasmicRootProvider loader={PLASMIC}>
      <PlasmicComponent component="COMPONENTNAME" />
    </PlasmicRootProvider>
  );
}


// You can register any code components that you want to use here; see
// https://docs.plasmic.app/learn/code-components-ref/
// And configure your Plasmic project to use the host url pointing at
// the /plasmic-host page of your nextjs app (for example,
// http://localhost:3000/plasmic-host).  See
// https://docs.plasmic.app/learn/app-hosting/#set-a-plasmic-project-to-use-your-app-host

// PLASMIC.registerComponent(...);
