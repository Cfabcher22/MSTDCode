using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace GUI_1
{
    public partial class Form1 : Form
    {
        bool viewData = false;

        public Form1()
        {
            InitializeComponent();
            tabControl1.Appearance = TabAppearance.FlatButtons;
            tabControl1.ItemSize = new Size(0, 1);       // Make tabs 1px tall
            tabControl1.SizeMode = TabSizeMode.Fixed;    // Prevent resizing
            tabControl1.Multiline = false;               // Single row of tabs
        }

        private void comboBox1_SelectedIndexChanged(object sender, EventArgs e)
        {

        }

        private void button1_Click(object sender, EventArgs e)
        {
            tabControl1.SelectedTab = tabPage2;
        }

        private void button3_Click(object sender, EventArgs e)
        {
            viewData = true;
            button9.Text = "View Data";
            tabControl1.SelectedTab = tabPage4;
        }

        private void button2_Click(object sender, EventArgs e)
        {

        }
        private void button4_Click(object sender, EventArgs e)
        {
            tabControl1.SelectedTab = tabPage5;
        }
        private void button5_Click(object sender, EventArgs e)
        {
            tabControl1.SelectedTab = tabPage3;
        }

        private void button6_Click(object sender, EventArgs e)
        {
            viewData = false;
            button9.Text = "Select Test";
            tabControl1.SelectedTab = tabPage4;
        }

        private void label2_Click(object sender, EventArgs e)
        {

        }

        private void label3_Click(object sender, EventArgs e)
        {

        }

        private void tabPage3_Click(object sender, EventArgs e)
        {

        }

        private void label5_Click(object sender, EventArgs e)
        {

        }

        private void label4_Click(object sender, EventArgs e)
        {

        }

        private void textBox5_TextChanged(object sender, EventArgs e)
        {

        }

        private void textBox4_TextChanged(object sender, EventArgs e)
        {

        }

        private void richTextBox1_TextChanged(object sender, EventArgs e)
        {

        }

        private void textBox6_TextChanged(object sender, EventArgs e)
        {

        }

        private void label13_Click(object sender, EventArgs e)
        {

        }

        private void button20_Click(object sender, EventArgs e)
        {
            tabControl1.SelectedTab = tabPage1;
        }

        private void button24_Click(object sender, EventArgs e)
        {
            tabControl1.SelectedTab = tabPage4;
        }

        private void button23_Click(object sender, EventArgs e)
        {
            tabControl1.SelectedTab = tabPage1;
        }

        private void button22_Click(object sender, EventArgs e)
        {
            if (viewData)
            {
                tabControl1.SelectedTab = tabPage1;
            }
            else
            {
                tabControl1.SelectedTab = tabPage2;
            }

            listBox1.SelectedIndex = -1;
        }

        private void button21_Click(object sender, EventArgs e)
        {
            tabControl1.SelectedTab = tabPage2;
        }

        private void tabPage5_Click(object sender, EventArgs e)
        {

        }

        private void button9_Click(object sender, EventArgs e)
        {
            if (viewData)
            {
                tabControl1.SelectedTab = tabPage6;
            }
            else
            {
                tabControl1.SelectedTab = tabPage7;
            }
        }

        private void listBox1_SelectedIndexChanged(object sender, EventArgs e)
        {
            button9.Enabled = listBox1.SelectedIndex != -1;
        }
        private void listBox2_SelectedIndexChanged(object sender, EventArgs e)
        {
            button8.Enabled = listBox2.SelectedIndex != -1;
        }

        private void button25_Click(object sender, EventArgs e)
        {
            listBox2.SelectedIndex = -1;
            tabControl1.SelectedTab = tabPage4;
        }

        private void label22_Click(object sender, EventArgs e)
        {

        }
    }
}
