/*
This file is part of CDP Front-end.

CDP front-end is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

CDP front-end is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with CDP front-end.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef JCDP_ENVELOPE_H
#define JCDP_ENVELOPE_H

#include <vector>
#include <algorithm>
#include "JuceHeader.h"
#include "jcdp_utilities.h"

struct envelope_node
{
    envelope_node()
        : Time(0.0), Value(0.0), Shape(0), Status(0),ShapeParam1(0.5), ShapeParam2(0.5) {}
    envelope_node(double x, double y, double p1=0.5, double p2=0.5)
        : Time(x), Value(y),ShapeParam1(p1),ShapeParam2(p2) {}
    double Time;
    double Value;
    int Shape;
    double ShapeParam1;
    double ShapeParam2;
    int Status;

};

inline bool operator<(const envelope_node& a, const envelope_node& b)
{
    return a.Time<b.Time;
}

struct grid_entry
{
    grid_entry(double v) : m_value(v) {}
    double m_value=0.0;
    bool m_foo=false;
};

inline double grid_value(const grid_entry& ge)
{
    return ge.m_value;
}

inline bool operator<(const grid_entry& a, const grid_entry& b)
{
    return a.m_value<b.m_value;
}

using grid_t=std::vector<grid_entry>;

//#define BEZIER_EXPERIMENT

inline double get_shaped_value(double x, int, double p1, double)
{
#ifndef BEZIER_EXPERIMENT
    if (p1<0.5)
    {
        double foo=1.0-(p1*2.0);
        return 1.0-pow(1.0-x,1.0+foo*4.0);
    }
    double foo=(p1-0.5)*2.0;
    return pow(x,1.0+foo*4.0);
#else
    /*
    double pt0=-2.0*p1;
    double pt1=2.0*p2;
    double pt2=1.0;
    return pow(1-x,2.0)*pt0+2*(1-x)*x*pt1+pow(x,2)*pt2;
    */
    if (p2<=0.5)
    {
        if (p1<0.5)
        {
            double foo=1.0-(p1*2.0);
            return 1.0-pow(1.0-x,1.0+foo*4.0);
        }
        double foo=(p1-0.5)*2.0;
        return pow(x,1.0+foo*4.0);
    } else
    {
        if (p1<0.5)
        {
            if (x<0.5)
            {
                x*=2.0;
                p1*=2.0;
                return 0.5*pow(x,p1*4.0);
            } else
            {
                x-=0.5;
                x*=2.0;
                p1*=2.0;
                return 1.0-0.5*pow(1.0-x,p1*4.0);
            }
        } else
        {
            if (x<0.5)
            {
                x*=2.0;
                p1-=0.5;
                p1*=2.0;
                return 0.5-0.5*pow(1.0-x,p1*4.0);
            } else
            {
                x-=0.5;
                x*=2.0;
                p1-=0.5;
                p1*=2.0;
                return 0.5+0.5*pow(x,p1*4.0);
            }
        }
    }
    return x;
#endif
}

using nodes_t=std::vector<envelope_node>;

inline double GetInterpolatedNodeValue(const nodes_t& m_nodes, double atime, double m_defvalue=0.5)
{
    int maxnodeind=m_nodes.size()-1;
    if (m_nodes.size()==0) return m_defvalue;
    if (m_nodes.size()==1) return m_nodes[0].Value;
    if (atime<=m_nodes[0].Time)
        return m_nodes[0].Value;
    if (atime>m_nodes[maxnodeind].Time)
        return m_nodes[maxnodeind].Value;
    const envelope_node to_search(atime,0.0);
    //to_search.Time=atime;
    auto it=std::lower_bound(m_nodes.begin(),m_nodes.end(),to_search,
                             [](const envelope_node& a, const envelope_node& b)
    { return a.Time<b.Time; } );
    if (it==m_nodes.end())
    {
        return m_defvalue;
    }
    --it; // lower_bound has returned iterator to point one too far
    double t1=it->Time;
    double v1=it->Value;
    double p1=it->ShapeParam1;
    double p2=it->ShapeParam2;
    ++it; // next envelope point
    double tdelta=it->Time-t1;
    if (tdelta<0.00001)
        tdelta=0.00001;
    double vdelta=it->Value-v1;
    return v1+vdelta*get_shaped_value(((1.0/tdelta*(atime-t1))),0,p1,p2);

}

inline double interpolate_foo(double atime,double t0, double v0, double t1, double v1, double p1, double p2)
{
    double tdelta=t1-t0;
    if (tdelta<0.00001)
        tdelta=0.00001;
    double vdelta=v1-v0;
    return v0+vdelta*get_shaped_value(((1.0/tdelta*(atime-t0))),0,p1,p2);
}

class breakpoint_envelope
{
public:
    breakpoint_envelope() : m_name("invalid") {}
    breakpoint_envelope(String name, double minv=0.0, double maxv=1.0)
        : m_minvalue(minv), m_maxvalue(maxv), m_name(name)
    {
        m_defshape=0;
        //m_color=RGB(0,255,255);
        m_defvalue=0.5;
        m_updateopinprogress=false;
        m_value_grid={0.0,0.25,0.5,0.75,1.0};
    }


    void SetName(String Name) { m_name=Name; }
    const String& GetName() const { return m_name; }
    double GetDefValue() { return m_defvalue; }
    void SetDefValue(double value) { m_defvalue=value; }
    int GetDefShape() { return m_defshape; }
    int GetNumNodes() const { return m_nodes.size(); }
    void SetDefShape(int value) { m_defshape=value; }
    const std::vector<envelope_node>& get_all_nodes() const { return m_nodes; }
    //void set_all_nodes(nodes_t& nds) { m_nodes=nds; }
    void set_reset_nodes(const std::vector<envelope_node>& nodes, bool convertvalues=false)
    {
        if (convertvalues==false)
            m_reset_nodes=nodes;
        else
        {
            if (scaled_to_normalized_func)
            {
                m_nodes.clear();
                for (int i=0;i<nodes.size();++i)
                {
                    envelope_node node=nodes[i];
                    node.Value=scaled_to_normalized_func(node.Value);
                    m_nodes.push_back(node);
                }
            }
        }
    }
    void ResetEnvelope()
    {
        m_nodes=m_reset_nodes;
        m_playoffset=0.0;
    }
    int GetColor()
    {
        return m_color;
    }
    void SetColor(int color)
    {
        m_color=color;
    }
    void BeginUpdate() // used for doing larger update operations, so can avoid needlessly sorting etc
    {
        m_updateopinprogress=true;
    }
    void EndUpdate()
    {
        m_updateopinprogress=false;
        SortNodes();
    }
    void AddNode(envelope_node newnode)
    {
        m_nodes.push_back(newnode);
        if (!m_updateopinprogress)
            SortNodes();
    }
    void ClearAllNodes()
    {
        m_nodes.clear();
    }
    void DeleteNode(int indx)
    {
        if (indx<0 || indx>m_nodes.size()-1)
            return;
        m_nodes.erase(m_nodes.begin()+indx);
    }
    void delete_nodes_in_time_range(double t0, double t1)
    {
        m_nodes.erase(std::remove_if(std::begin(m_nodes),
                                       std::end(m_nodes),
                                       [t0,t1](const envelope_node& a) { return a.Time>=t0 && a.Time<=t1; } ),
                                       std::end(m_nodes) );
    }

    envelope_node& GetNodeAtIndex(int indx)
    {
        if (m_nodes.size()==0)
        {
            throw(std::length_error("Empty envelope accessed"));
        }
        if (indx<0)
            indx=0;
        if (indx>=m_nodes.size())
            indx=m_nodes.size()-1;
        return m_nodes[indx];
    }
    const envelope_node& GetNodeAtIndex(int indx) const
    {
        if (m_nodes.size()==0)
        {
            throw(std::length_error("Empty envelope accessed"));
        }
        if (indx<0)
            indx=0;
        if (indx>=m_nodes.size())
            indx=m_nodes.size()-1;
        return m_nodes[indx];
    }
    void SetNodeStatus(int indx, int nstatus)
    {
        int i=indx;
        if (indx<0) i=0;
        if (indx>m_nodes.size()-1) i=m_nodes.size()-1;
        m_nodes[i].Status=nstatus;
    }
    void SetNode(int indx, envelope_node anode)
    {
        int i=indx;
        if (indx<0) i=0;
        if (indx>m_nodes.size()-1) i=m_nodes.size()-1;
        m_nodes[i]=anode;
    }
    void SetNodeTimeValue(int indx,bool setTime,bool setValue,double atime,double avalue)
    {
        int i=indx;
        if (indx<0) i=0;
        if (indx>m_nodes.size()-1) i=m_nodes.size()-1;
        if (setTime) m_nodes[i].Time=atime;
        if (setValue) m_nodes[i].Value=avalue;
    }


    double GetInterpolatedNodeValue(double atime)
    {
        double t0=0.0;
        double t1=0.0;
        double v0=0.0;
        double v1=0.0;
        double p1=0.0;
        double p2=0.0;
        const int maxnodeind=m_nodes.size()-1;
        if (m_nodes.size()==0)
            return m_defvalue;
        if (m_nodes.size()==1)
            return m_nodes[0].Value;
        if (atime<=m_nodes[0].Time)
        {
#ifdef INTERPOLATING_ENVELOPE_BORDERS
            t1=m_nodes[0].Time;
            t0=0.0-(1.0-m_nodes[maxnodeind].Time);
            v0=m_nodes[maxnodeind].Value;
            p1=m_nodes[maxnodeind].ShapeParam1;
            p2=m_nodes[maxnodeind].ShapeParam2;
            v1=m_nodes[0].Value;
            return interpolate_foo(atime,t0,v0,t1,v1,p1,p2);
#else
            return m_nodes[0].Value;
#endif
        }
        if (atime>m_nodes[maxnodeind].Time)
        {
#ifdef INTERPOLATING_ENVELOPE_BORDERS
            t0=m_nodes[maxnodeind].Time;
            t1=1.0+(m_nodes[0].Time);
            v0=m_nodes[maxnodeind].Value;
            v1=m_nodes[0].Value;
            p1=m_nodes[maxnodeind].ShapeParam1;
            p2=m_nodes[maxnodeind].ShapeParam2;
            return interpolate_foo(atime,t0,v0,t1,v1,p1,p2);
#else
            return m_nodes.back().Value;
#endif
        }
        const envelope_node to_search(atime,0.0);
        //to_search.Time=atime;
        auto it=std::lower_bound(m_nodes.begin(),m_nodes.end(),to_search,
                                 [](const envelope_node& a, const envelope_node& b)
        { return a.Time<b.Time; } );
        if (it==m_nodes.end())
        {
            return m_defvalue;
        }
        --it; // lower_bound has returned iterator to point one too far
        t0=it->Time;
        v0=it->Value;
        p1=it->ShapeParam1;
        p2=it->ShapeParam2;
        ++it; // next envelope point
        t1=it->Time;
        v1=it->Value;
        return interpolate_foo(atime,t0,v0,t1,v1,p1,p2);
    }
    bool IsSorted() const
    {
        return std::is_sorted(m_nodes.begin(), m_nodes.end(), []
                              (const envelope_node& lhs, const envelope_node& rhs)
                              {
                                  return lhs.Time<rhs.Time;
                              });
    }
    void SortNodes()
    {
        stable_sort(m_nodes.begin(),m_nodes.end(),
             [](const envelope_node& a, const envelope_node& b){ return a.Time<b.Time; } );
    }
    double minimum_value() const { return m_minvalue; }
    double maximum_value() const { return m_maxvalue; }
    void set_minimum_value(double v) { m_minvalue=v; }
    void set_maximum_value(double v) { m_maxvalue=v; }
    std::function<double(double)> normalized_to_scaled_func;
    std::function<double(double)> scaled_to_normalized_func;
    void begin_transformation()
    {
        m_old_nodes=m_nodes;
    }
    void end_transformation()
    {
        m_old_nodes.clear();
    }
    nodes_t& origin_nodes()
    {
        return m_old_nodes;
    }
    const nodes_t& repeater_nodes() const
    {
        return m_repeater_nodes;
    }
    void store_repeater_nodes()
    {
        m_repeater_nodes.clear();
        for (int i=0;i<m_nodes.size();++i)
        {
            if (m_nodes[i].Time>=m_playoffset && m_nodes[i].Time<=m_playoffset+1.0)
            {
                envelope_node temp=m_nodes[i];
                temp.Time-=m_playoffset;
                m_repeater_nodes.push_back(temp);
            }
        }
    }
    double get_play_offset() const { return m_playoffset; }
    //void set_play_offset(double x) { m_playoffset=bound_value(m_mintime,x,m_maxtime); }
    //time_range get_play_offset_range() const { return std::make_pair(m_mintime,m_maxtime); }
    const grid_t& get_value_grid() const { return m_value_grid; }
    void set_value_grid(grid_t g) { m_value_grid=std::move(g); }
    template<typename F>
    void manipulate(F&& f)
    {
        nodes_t backup=m_nodes;
        if (f(backup)==true)
        {
            std::swap(backup,m_nodes);
            SortNodes();
        }
    }
private:
    nodes_t m_nodes;
    double m_playoffset=0.0;
    double m_minvalue=0.0;
    double m_maxvalue=1.0;
    double m_mintime=-2.0;
    double m_maxtime=2.0;
    int m_defshape;
    int m_color;
    String m_name;
    bool m_updateopinprogress;
    double m_defvalue; // "neutral" value to be used for resets and stuff

    nodes_t m_reset_nodes;
    nodes_t m_old_nodes;
    nodes_t m_repeater_nodes;
    grid_t m_value_grid;
};

#endif // JCDP_ENVELOPE_H
