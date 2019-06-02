#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <utility>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <random>
#include <fstream>

#include "Vec3d.h"
#include "Mesh.h"
#include "GridData.h"
#include "Triangle.h"
#include "Grid.h"
#include "Camera.h"
#include "CameraData.h"
#include "Image.h"
#include "Light.h"

#include "material/BRDF.h"
#include "material/Material.h"

#include "Constants.h"

#ifdef FPGA
#   include "admxrc3.h"
#   define XINTERSECTFPGA_CONTROL_ADDR_AP_CTRL            0x00
#   define XINTERSECTFPGA_CONTROL_ADDR_GIE                0x04
#   define XINTERSECTFPGA_CONTROL_ADDR_IER                0x08
#   define XINTERSECTFPGA_CONTROL_ADDR_ISR                0x0c
#   define XINTERSECTFPGA_CONTROL_ADDR_I_TNUMBER_DATA     0x10
#   define XINTERSECTFPGA_CONTROL_BITS_I_TNUMBER_DATA     32
#   define XINTERSECTFPGA_CONTROL_ADDR_I_TDATA_DATA       0x18
#   define XINTERSECTFPGA_CONTROL_BITS_I_TDATA_DATA       32
#   define XINTERSECTFPGA_CONTROL_ADDR_I_TIDS_DATA        0x20
#   define XINTERSECTFPGA_CONTROL_BITS_I_TIDS_DATA        32
#   define XINTERSECTFPGA_CONTROL_ADDR_I_RNUMBER_DATA     0x28
#   define XINTERSECTFPGA_CONTROL_BITS_I_RNUMBER_DATA     32
#   define XINTERSECTFPGA_CONTROL_ADDR_I_RDATA_DATA       0x30
#   define XINTERSECTFPGA_CONTROL_BITS_I_RDATA_DATA       32
#   define XINTERSECTFPGA_CONTROL_ADDR_O_TIDS_DATA        0x38
#   define XINTERSECTFPGA_CONTROL_BITS_O_TIDS_DATA        32
#   define XINTERSECTFPGA_CONTROL_ADDR_O_TINTERSECTS_DATA 0x40
#   define XINTERSECTFPGA_CONTROL_BITS_O_TINTERSECTS_DATA 32
#endif

#define PV(A) cout<<#A<<" = "<<A<endl
#define FOR(I,N) for(int I = 0; I < N; I++)

//#define INV_PI 0.318309886

#define TRUE (0==0)
#define FALSE (0==1)

void prepareRays(double* rData, const Camera& cam)
{
    int hres = cam.getHorizontalResolution();
    int vres = cam.getVerticalResolution();
    for(int r = 0; r < vres; r++)
    {
        for(int c = 0; c < hres; c++)
        {
            int base = ((r * hres) + c) * 6;
            auto&& ray = cam.getRay(r, c);
            rData[base + 0] = ray.origin.x;
            rData[base + 1] = ray.origin.y;
            rData[base + 2] = ray.origin.z;
            rData[base + 3] = ray.direction.x;
            rData[base + 4] = ray.direction.y;
            rData[base + 5] = ray.direction.z;
        }
    }
}


void prepareTriangles(double *tData, int *idData, const std::shared_ptr<Mesh>& m)
{
    for(int i = 0; i < m->triangles.size(); i++)
    {
        idData[i] = i;
        int base = i * 9;
        tData[base + 0] = m->triangles[i]->p1.x;
        tData[base + 1] = m->triangles[i]->p1.y;
        tData[base + 2] = m->triangles[i]->p1.z;
        tData[base + 3] = m->triangles[i]->p2.x;
        tData[base + 4] = m->triangles[i]->p2.y;
        tData[base + 5] = m->triangles[i]->p2.z;
        tData[base + 6] = m->triangles[i]->p3.x;
        tData[base + 7] = m->triangles[i]->p3.y;
        tData[base + 8] = m->triangles[i]->p3.z;
    }
}

#include "DarkRenderer/Globals.h"

#include "Application/Session.h"

class Tracer{
protected:
    Application::Session& m_session;

public:     
    Tracer(Application::Session& sess) :
        m_session(sess){}

    virtual void render(int frame) const = 0;
};

class CPUTracer : public Tracer{
public:     
    CPUTracer(Application::Session& sess) :
        Tracer(sess) {}

    virtual void render(int frame) const
    {

        if(m_session.meshes.size() > 0 && m_session.materials.size() > 0)
        {
            std::cout << "LOG: Beginning to process in the CPU" << std::endl;
            std::cout 
                << "LOG: Image of size "
                << m_session.camera->getHorizontalResolution() 
                << "x" 
                << m_session.camera->getVerticalResolution() 
                << std::endl;

            int r, c;
            for(r = 0; r < m_session.camera->getVerticalResolution(); r++)
            for(c = 0; c < m_session.camera->getHorizontalResolution(); c++)
            {
                Color L;
                Ray ray = m_session.camera->getRay(r, c);
    
                Intersection it;
    
                double tMin = Globals::INFINITY;
                int idMin = -1, iMin = -1;
                for(int i = 0; i < m_session.meshes[0].triangles.size(); i++)
                {
                    double t;
                    bool hit = m_session.meshes[0].triangles[i]->hit(ray, t);
                    if(hit && t < tMin)
                    {
                        idMin = m_session.meshes[0].triangles[i]->getId();
                        tMin = t;
                        iMin = i;

                        it.t = t;
                        it.triangleId = idMin;
    
                    }
                }
    
                if(idMin != -1)
                {
    
                    it.normalVector = m_session.meshes[0].triangles[iMin]->normal;
                    it.hit = true;
                    it.rayDirection = ray.direction;
                    it.hitPoint = ray.rayPoint(it.t);
                    it.triangleId = iMin;
    
                    L = m_session.materials[0]->shade(it, m_session.lights);
    
                }
                m_session.frames[0].setPixel(c, r, L.clamp());
            }
        }
    }
};

#ifdef FPGA
class FPGATracer : public Tracer
{
public:
    FPGATracer(Application::Session& sess) :
        Tracer(sess)
        {}
    
    ~FPGATracer();
    
    virtual void render(int frame) const{
        /// Initializing the basic information of the raytracing session
        int nRays = vres * hres, nTris = m->triangles.size();
        const int FPGA_TRI_ATTR_NUMBER = 9;

        const int FPGA_MAX_TRIS = 50000;
        const int FPGA_MAX_RAYS = 921600;

        unsigned long index = 0;
        ADMXRC3_STATUS status;
        ADMXRC3_HANDLE hCard = ADMXRC3_HANDLE_INVALID_VALUE;

        uint64_t NUM_RAYS = nRays;
        uint64_t RAY_SIZE = 6;
        uint64_t MAX_RAYS = 921600;

        uint64_t NUM_TRIS = nTris;
        uint64_t TRI_SIZE = 9;
        uint64_t MAX_TRIS = 50000;

        double *rData = nullptr, *tData = nullptr, *outInter = nullptr;
        int *idData = nullptr, *outIds = nullptr;

        /// allocating the memory needed
        try
        {
            rData = new double[nRays * Ray::NUM_ATTRIBUTES];
            tData = new double[nTris * FPGA_TRI_ATTR_NUMBER];
            idData = new int[nTris];
            outIds = new int[nRays];
            outInter = new double[nRays];
        }
        catch(const std::bad_alloc& al)
        {
            if(rData != nullptr)
            {
                delete[] rData;
            }

            if(tData != nullptr)
            {
                delete[] tData;
            }
            
            if(idData != nullptr)
            {
                delete[] idData;
            }
            
            if(outIds != nullptr)
            {
                delete[] outIds;
            }
            
            if(outInter != nullptr)
            {
                delete[] outInter;
            }

            cerr << "It wasn't possible to alloc a memory of this size...\nMessage: ";
            cerr << al.what() << "\n";
            exit(EXIT_FAILURE);
        }

        /// preparing data structures to send to the FPGA
        prepareRays(rData, cam);
        prepareTriangles(tData, idData, m);

        /// Declaring the base addresses of the arrays in the
        /// FPGA DDR memory
        uint64_t addr_tris = 0;
        uint64_t addr_ids = sizeof(double) * FPGA_MAX_TRIS * TRI_SIZE;
        uint64_t addr_rays = addr_ids + (sizeof(int) * FPGA_MAX_TRIS);
        uint64_t addr_outInter = addr_rays + (sizeof(double) * FPGA_MAX_RAYS * Ray::NUM_ATTRIBUTES);
        uint64_t addr_outIds = addr_outInter + (sizeof(double) * FPGA_MAX_RAYS);
        uint64_t ctrl = 0;

        /// Opening FPGA card
        status = ADMXRC3_Open(0, &hCard);
        if (status != ADMXRC3_SUCCESS) {
            fprintf(
                stderr,
                "Failed to open card with index %lu: %s\n",
                (unsigned long) index,
                ADMXRC3_GetStatusString(status, TRUE)
            );
            return -1;
        }

        /// Getting the status of the Accelerator by reading the CTRL address
        printf("IP Status:\n");
        ADMXRC3_Read(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_AP_CTRL, sizeof(uint64_t), &ctrl);
        printf("status 0x%lx\n",ctrl);

        /// Writing the new command (idle)
        ADMXRC3_Write(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_AP_CTRL, sizeof(uint64_t), &ctrl);

        /// Writing to the input addresses
        /// Number of Triangles
        ADMXRC3_Write(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_I_TNUMBER_DATA, sizeof(uint64_t), &nTris);
        /// idData base address
        ADMXRC3_Write(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_I_TIDS_DATA, sizeof(uint64_t), &addr_ids);
        /// tData base address
        ADMXRC3_Write(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_I_TDATA_DATA, sizeof(uint64_t), &addr_tris);
        /// Number of Rays
        ADMXRC3_Write(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_I_RNUMBER_DATA, sizeof(uint64_t), &nRays);
        /// rData base address
        ADMXRC3_Write(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_I_RDATA_DATA, sizeof(uint64_t), &addr_rays);
        /// outIds base address - triangle id output
        ADMXRC3_Write(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_O_TIDS_DATA, sizeof(uint64_t), &addr_outIds);
        /// outInter base address - triangle intersection distance output
        ADMXRC3_Write(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_O_TINTERSECTS_DATA, sizeof(uint64_t), &addr_outInter);


        /// FPGA Memory copy of the input arrays via DMA
        ///---------------------------------------------------
        ///---------------------dma writes--------------------
        ///---------------------------------------------------

        /// Writing tData
        status = ADMXRC3_WriteDMA(hCard, 0, 0, tData, nTris * FPGA_TRI_ATTR_NUMBER * sizeof(double), addr_tris);
        if (status != ADMXRC3_SUCCESS) {
            fprintf(stderr,"Triangle data write DMA transfer failed: %s\n", ADMXRC3_GetStatusString(status, TRUE));
            return -1;
        }

        /// Writing idData
        status = ADMXRC3_WriteDMA(hCard, 0, 0, idData, NUM_TRIS * sizeof(int), addr_ids);
        if (status != ADMXRC3_SUCCESS) {
            fprintf(stderr,"Id data write DMA transfer failed: %s\n", ADMXRC3_GetStatusString(status, TRUE));
            return -1;
        }


        /// Writing rData
        status = ADMXRC3_WriteDMA(hCard, 0, 0, rData, NUM_RAYS * RAY_SIZE * sizeof(double), addr_rays);
        if (status != ADMXRC3_SUCCESS) {
            fprintf(stderr,"Triangle data write DMA transfer failed: %s\n", ADMXRC3_GetStatusString(status, TRUE));
            return -1;
        }
        printf("write dma done!\n");


        ADMXRC3_Read(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_AP_CTRL, sizeof(uint64_t), &ctrl);

        if(ctrl == 4)
        {
                printf("core is idle, ctrl = %ld\n",ctrl);
        }

        ADMXRC3_Read(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_AP_CTRL, sizeof(uint64_t), &ctrl);

        if(ctrl == 4)
        {
            cout << "core is idle, ctrl = " << ctrl << "\n";
        }

        /// Write control value = 1 (start)
        ctrl = 1;
        ADMXRC3_Write(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_AP_CTRL, sizeof(uint64_t), &ctrl);

        /// Polling the FPGA for the results
        ADMXRC3_Read(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_AP_CTRL, sizeof(uint64_t), &ctrl);
        while(ctrl != 4)
        {
            ADMXRC3_Read(hCard, 0, 0, XINTERSECTFPGA_CONTROL_ADDR_AP_CTRL, sizeof(uint64_t), &ctrl);
        }
        printf("done!\n");


        /// Getting the result back from the FPGA

        /// Id of the triangles hit by the sent rays
        status = ADMXRC3_ReadDMA(hCard, 0, 0, outIds, NUM_RAYS * sizeof(int), addr_outIds);
        if (status != ADMXRC3_SUCCESS) {
            fprintf(stderr,"outIds read DMA transfer failed: %s\n", ADMXRC3_GetStatusString(status, TRUE));
            return -1;
        }

        /// Intersection parameter of where the triangle was hit
        status = ADMXRC3_ReadDMA(hCard, 0, 0, outInter, NUM_RAYS * sizeof(double), addr_outInter);
        if (status != ADMXRC3_SUCCESS) {
            fprintf(stderr,"outInter read DMA transfer failed: %s\n", ADMXRC3_GetStatusString(status, TRUE));
            return -1;
        }

        int r, c;
        for(r = 0; r < vres; r++)
        for(c = 0; c < hres; c++)
        {
            Color L;
            /// Identifier of the ray that was sent to the FPGA
            int rayId = r * hres + c;
            /// Identifier of the triangle hit by the current ray
            int hitId = outIds[rayId];

            Intersection it;

            Ray ray = cam.getRay(r, c);

            if(hitId != -1)
            {
                Vec3d wo =
                    Vec3d(
                        rData[rayId * Ray::NUM_ATTRIBUTES + 3],
                        rData[rayId * Ray::NUM_ATTRIBUTES + 4],
                        rData[rayId * Ray::NUM_ATTRIBUTES + 5]
                    );// -ray.direction;

                //double dot = m->triangles[hitId]->normal * wo;
                //Color lightInfluence = light->mColor * light->mIntensity;
                //L = (matCol * matDif * INV_PI) * lightInfluence * dot;

                it.normalVector = m->triangles[hitId]->normal;
                it.hit = true;
                it.rayDirection = wo;
                it.t = outInter[rayId];
                it.hitPoint = ray.rayPoint(it.t);
                it.triangleId = hitId;

                L = materialData[0]->shade(it, lights);
            }

            sess.frames[0].setPixel(c, r, L);

        }
      

        status = ADMXRC3_Close(hCard);
        if (status != ADMXRC3_SUCCESS) {
            fprintf(stderr,"Failed to close card with index %lu: %s\n", (unsigned long)index, ADMXRC3_GetStatusString(status, TRUE));
            return -1;

        delete[] rData;
        delete[] tData;
        delete[] idData;
        delete[] outIds;
        delete[] outInter;
    }

};
#endif



int main(int argc, char** argv){

    Application::Session& sess = DarkRenderer::session;

    DarkRenderer::InitParser(argc, argv);

    DarkRenderer::InitSession();

    clock_t fulltime_t0 = clock(), fulltime_tf;

    struct timespec start, stop;

    clock_gettime(CLOCK_MONOTONIC, &start);

    std::shared_ptr<Tracer> tracer;

    if(!DarkRenderer::session.useFPGA)
    {
        tracer = std::make_shared<CPUTracer>(sess);
    }
    else
    {
        #ifdef FPGA
        tracer = std::make_shared<FPGATracer>(sess);
        #else
        std::cerr << "ERROR: FPGA usage not compiled in this version:\n"
                  << "Please recompile with the command line option -DFPGA or run using\n"
                  << "the CPU instead.\n\n";

        exit(EXIT_FAILURE);
        #endif

    }
    

    tracer->render();
    fulltime_tf = clock();

    std::cout 
        << "Total elapsed time: " 
        << (double) (fulltime_tf - fulltime_t0)/CLOCKS_PER_SEC 
        << "\n";
        
    clock_gettime(CLOCK_MONOTONIC, &stop);
    float tm = (stop.tv_sec - start.tv_sec) + (stop.tv_nsec - start.tv_nsec) / 1e9;
    printf("Rendering %s with resolution %dx%d took %f seconds\n",
            DarkRenderer::session.outputName.c_str(),
            sess.camera->getHorizontalResolution(),
            sess.camera->getVerticalResolution(),
            tm);

    std::cout << "Saving " << DarkRenderer::session.outputName <<"\n";
    if(sess.frames.size() > 0)
        sess.frames[0].save(sess.outputName.c_str());

    return 0;
}
