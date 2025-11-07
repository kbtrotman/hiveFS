import { initPlasmicLoader } from "@plasmicapp/loader-nextjs";

export const PLASMIC = initPlasmicLoader({
  projects: [
    {
      id: "c8gT28iq7v39gxBJ6CcbGP",  // ID of a project you are using
      token: "vgOBIsGV4sdbGDBdw0krBZbdIysWmwJvtCetA133bMT6cArukHVSobrFAD6rmQeTnlcSGbuX77IF1j7GWA"  // API token for that project
    }
  ],
  // Fetches the latest revisions, whether or not they were unpublished!
  // Disable for production to ensure you render only published changes.
  preview: true,
})